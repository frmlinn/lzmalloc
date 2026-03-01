/**
 * @file vmm.c
 * @brief Implementation of the Virtual Memory Manager with guaranteed alignment.
 */

#define _GNU_SOURCE // For MAP_ANONYMOUS, MADV_HUGEPAGE on Linux
#include "vmm.h"
#include <sys/mman.h>
#include <stdatomic.h>
#include <unistd.h>

/* ========================================================================= *
 * Internal State and Security
 * ========================================================================= */

/**
 * @brief Micro-Spinlocks to protect NUMA pools from the ABA problem.
 * atomic_flag is the only lock-free guaranteed primitive by C11 standard.
 */
static atomic_flag g_pool_locks[LZ_MAX_NUMA_NODES];

/* ========================================================================= *
 * Helper: NUMA-Local Spinlock
 * ========================================================================= */

static LZ_ALWAYS_INLINE void pool_lock(uint32_t node_id) {
    while (atomic_flag_test_and_set_explicit(&g_pool_locks[node_id], memory_order_acquire)) {
        lz_cpu_relax();
    }
}

static LZ_ALWAYS_INLINE void pool_unlock(uint32_t node_id) {
    atomic_flag_clear_explicit(&g_pool_locks[node_id], memory_order_release);
}

/* ========================================================================= *
 * Helper: OS Allocation with Over-allocation Strategy
 * ========================================================================= */

static void* sys_alloc_aligned_2mb(void) {
    // Request DOUBLE the size to guarantee a 2MB aligned boundary exists inside.
    size_t request_size = LZ_HUGE_PAGE_SIZE * 2;
    
    void* raw_ptr = mmap(NULL, request_size, PROT_READ | PROT_WRITE, 
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                         
    if (LZ_UNLIKELY(raw_ptr == MAP_FAILED)) {
        return NULL;
    }

    uintptr_t raw_addr = (uintptr_t)raw_ptr;
    
    // Fast path: If the OS miraculously gave us a perfectly aligned address
    if (LZ_LIKELY((raw_addr & (LZ_HUGE_PAGE_SIZE - 1)) == 0)) {
        // Return the excess 2MB back to the Kernel
        munmap((void*)(raw_addr + LZ_HUGE_PAGE_SIZE), LZ_HUGE_PAGE_SIZE);
#ifdef MADV_HUGEPAGE
        madvise((void*)raw_addr, LZ_HUGE_PAGE_SIZE, MADV_HUGEPAGE);
#endif
        return (void*)raw_addr;
    }

    // Slow path: Calculate the next aligned boundary
    uintptr_t aligned_addr = LZ_ALIGN_UP(raw_addr, LZ_HUGE_PAGE_SIZE);
    
    // Calculate prefix and suffix to trim
    size_t prefix_size = aligned_addr - raw_addr;
    size_t suffix_size = request_size - prefix_size - LZ_HUGE_PAGE_SIZE;

    // Unmap the garbage memory to keep only our pure 2MB Chunk
    if (prefix_size > 0) {
        munmap((void*)raw_addr, prefix_size);
    }
    if (suffix_size > 0) {
        munmap((void*)(aligned_addr + LZ_HUGE_PAGE_SIZE), suffix_size);
    }

#ifdef MADV_HUGEPAGE
    // Hint to the OS to use Transparent Huge Pages
    madvise((void*)aligned_addr, LZ_HUGE_PAGE_SIZE, MADV_HUGEPAGE);
#endif

    return (void*)aligned_addr;
}

/* ========================================================================= *
 * Public API Implementation
 * ========================================================================= */

void lz_vmm_init(void) {
    lz_topology_init();
    for (int i = 0; i < LZ_MAX_NUMA_NODES; ++i) {
        atomic_flag_clear(&g_pool_locks[i]);
    }
}

lz_chunk_header_t* lz_vmm_alloc_chunk(void) {
    uint32_t current_node = lz_get_current_node();
    lz_numa_pool_t* pool = lz_topology_get_pool(current_node);
    lz_chunk_header_t* chunk = NULL;

    pool_lock(current_node);
    lz_chunk_header_t* top = atomic_load_explicit(&pool->free_chunks, memory_order_relaxed);
    if (top) {
        chunk = top;
        // Correct atomic assignment inside spinlock
        atomic_store_explicit(&pool->free_chunks, chunk->next, memory_order_relaxed);
        atomic_fetch_sub_explicit(&pool->available_count, 1, memory_order_relaxed);
    }
    pool_unlock(current_node);

    if (LZ_LIKELY(chunk != NULL)) {
        chunk->next = NULL;
        return chunk;
    }

    chunk = (lz_chunk_header_t*)sys_alloc_aligned_2mb();
    if (LZ_UNLIKELY(!chunk)) return NULL; 

    // Neutral initialization for VMM metadata
    chunk->next = NULL;
    chunk->owning_tlh = NULL; 
    chunk->node_id = current_node;
    chunk->is_lsm_region = 0;
    chunk->magic = LZ_CHUNK_MAGIC_V2; 
    chunk->canary = 0;
    chunk->checksum = 0;

    return chunk;
}

void lz_vmm_free_chunk(lz_chunk_header_t* chunk) {
    if (LZ_UNLIKELY(!chunk || chunk->magic != LZ_CHUNK_MAGIC_V2)) return; 

    uint32_t target_node = chunk->node_id;
    lz_numa_pool_t* pool = lz_topology_get_pool(target_node);
    bool return_to_os = false;

    pool_lock(target_node);
    size_t count = atomic_load_explicit(&pool->available_count, memory_order_relaxed);
    if (count < LZ_VMM_MAX_CACHED_CHUNKS) {
        lz_chunk_header_t* top = atomic_load_explicit(&pool->free_chunks, memory_order_relaxed);
        chunk->next = top;
        atomic_store_explicit(&pool->free_chunks, chunk, memory_order_relaxed);
        atomic_fetch_add_explicit(&pool->available_count, 1, memory_order_relaxed);
    } else {
        return_to_os = true;
    }
    pool_unlock(target_node);

    if (LZ_UNLIKELY(return_to_os)) {
        munmap((void*)chunk, LZ_HUGE_PAGE_SIZE);
    }
}