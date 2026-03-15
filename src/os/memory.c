/**
 * @file memory.c
 * @brief Implementation of VMA primitives via mmap and madvise.
 * @details Manages kernel-level memory transactions. Focuses on forcing 
 * alignment via address space over-allocation and trimming.
 */
#define _GNU_SOURCE
#include "memory.h"
#include "lz_log.h"
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>

void* lz_os_alloc_aligned(size_t size, size_t alignment) {
    /* Mathematical assertion: Alignment MUST be a power of 2 for AND-masking */
    if (LZ_UNLIKELY(alignment == 0 || (alignment & (alignment - 1)) != 0)) {
        LZ_FATAL("Memory: VMA alignment request is not a power of 2: %zu", alignment);
        return NULL;
    }

    /* 1. Over-allocation: Ensure the boundary falls within the mapped range */
    size_t request_size = size + alignment;
    
    void* raw_ptr = mmap(NULL, request_size, PROT_READ | PROT_WRITE, 
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                         
    if (LZ_UNLIKELY(raw_ptr == MAP_FAILED)) {
        LZ_ERROR("Memory: mmap() failed for %zu bytes (Errno: %d). System OOM likely.", request_size, errno);
        return NULL;
    }

    uintptr_t raw_addr = (uintptr_t)raw_ptr;
    void* final_ptr = NULL;
    
    /* 2. Fast-Path: Kernel returns a perfectly aligned pointer by chance */
    if (LZ_LIKELY((raw_addr & (alignment - 1)) == 0)) {
        /* Unmap the excess suffix (Exactly 'alignment' bytes) */
        munmap((void*)(raw_addr + size), alignment);
        final_ptr = (void*)raw_addr;
    } 
    /* 3. Slow-Path: Manually force mathematical alignment and trim both ends */
    else {
        uintptr_t aligned_addr = (raw_addr + alignment - 1) & ~(alignment - 1);
        size_t prefix_size = aligned_addr - raw_addr;
        size_t suffix_size = request_size - prefix_size - size;

        if (prefix_size > 0) {
            munmap((void*)raw_addr, prefix_size);
        }
        if (suffix_size > 0) {
            munmap((void*)(aligned_addr + size), suffix_size);
        }
        final_ptr = (void*)aligned_addr;
    }

#if defined(__linux__) && defined(MADV_HUGEPAGE)
    /* Advisory: Request Transparent Huge Pages (THP) to reduce TLB pressure */
    if (madvise(final_ptr, size, MADV_HUGEPAGE) != 0) {
        LZ_DEBUG("Memory: MADV_HUGEPAGE ignored or disabled for block %p.", final_ptr);
    }
#endif

    LZ_DEBUG("Memory: VMA Aligned Alloc -> %p (Size: %zu, Align: %zu)", final_ptr, size, alignment);
    return final_ptr;
}

void lz_os_free(void* ptr, size_t size) {
    if (LZ_LIKELY(ptr)) {
        if (LZ_UNLIKELY(munmap(ptr, size) != 0)) {
            LZ_ERROR("Memory: munmap() failure at %p (Size: %zu). VMA leak imminent.", ptr, size);
        } else {
            LZ_DEBUG("Memory: VMA Free -> %p (Size: %zu)", ptr, size);
        }
    }
}

void lz_os_purge_physical(void* ptr, size_t size) {
    if (LZ_UNLIKELY(!ptr || size == 0)) return;

    /* Topological Absorption: Release physical backing pages */
#if defined(__linux__)
    /* MADV_DONTNEED: Synchronously drops pages and clears the resident bit */
    madvise(ptr, size, MADV_DONTNEED);
#elif defined(__APPLE__) || defined(__FreeBSD__)
    /* MADV_FREE: Lazy release; kernel reclaims pages only under pressure */
    madvise(ptr, size, MADV_FREE);
#else
    /* POSIX Fallback */
    posix_madvise(ptr, size, POSIX_MADV_DONTNEED);
#endif
}