/**
 * @file memory.c
 * @brief Implementación de las primitivas de mmap y madvise.
 */
#define _GNU_SOURCE
#include "memory.h"
#include "lz_log.h"
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>

void* lz_os_alloc_aligned(size_t size, size_t alignment) {
    /* Aserción matemática: El alignment DEBE ser una potencia de 2 para usar máscaras AND */
    if (LZ_UNLIKELY(alignment == 0 || (alignment & (alignment - 1)) != 0)) {
        LZ_FATAL("Memory: Se solicitó un alineamiento VMA que no es potencia de 2: %zu", alignment);
        return NULL; /* Inalcanzable tras LZ_FATAL, pero calma al compilador */
    }

    /* 1. Sobreasignación para garantizar que la frontera caiga dentro del mapeo */
    size_t request_size = size + alignment;
    
    void* raw_ptr = mmap(NULL, request_size, PROT_READ | PROT_WRITE, 
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                         
    if (LZ_UNLIKELY(raw_ptr == MAP_FAILED)) {
        LZ_ERROR("Memory: mmap() falló al solicitar %zu bytes (Errno: %d). OS OOM probable.", request_size, errno);
        return NULL;
    }

    uintptr_t raw_addr = (uintptr_t)raw_ptr;
    void* final_ptr = NULL;
    
    /* 2. Fast-Path OS: A veces el OS ya nos devuelve un puntero perfectamente alineado */
    if (LZ_LIKELY((raw_addr & (alignment - 1)) == 0)) {
        /* Recortamos el exceso del sufijo (Exactamente 'alignment' bytes extra) */
        munmap((void*)(raw_addr + size), alignment);
        final_ptr = (void*)raw_addr;
    } 
    /* 3. Slow-Path OS: Forzar alineamiento matemático y recortar extremos */
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
    /* Solicitar Transparent Huge Pages si el kernel lo permite */
    if (madvise(final_ptr, size, MADV_HUGEPAGE) != 0) {
        LZ_DEBUG("Memory: MADV_HUGEPAGE no habilitado o ignorado para el bloque %p.", final_ptr);
    }
#endif

    LZ_DEBUG("Memory: VMA Aligned Alloc -> %p (Size: %zu, Align: %zu)", final_ptr, size, alignment);
    return final_ptr;
}

void lz_os_free(void* ptr, size_t size) {
    if (LZ_LIKELY(ptr)) {
        if (LZ_UNLIKELY(munmap(ptr, size) != 0)) {
            LZ_ERROR("Memory: munmap() falló en %p (Size: %zu). Fuga de memoria VMA inminente.", ptr, size);
        } else {
            LZ_DEBUG("Memory: VMA Free -> %p (Size: %zu)", ptr, size);
        }
    }
}

void lz_os_purge_physical(void* ptr, size_t size) {
    if (LZ_UNLIKELY(!ptr || size == 0)) return;

    /* Absorción Topológica: Reducir el RSS del proceso */
#if defined(__linux__)
    /* MADV_DONTNEED en Linux libera sincrónicamente la memoria física */
    madvise(ptr, size, MADV_DONTNEED);
#elif defined(__APPLE__) || defined(__FreeBSD__)
    /* MADV_FREE es diferido (lazy) */
    madvise(ptr, size, MADV_FREE);
#else
    /* Fallback genérico POSIX */
    posix_madvise(ptr, size, POSIX_MADV_DONTNEED);
#endif
}