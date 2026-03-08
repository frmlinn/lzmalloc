/**
 * @file vmm.c
 * @brief Implementation of the Virtual Memory Manager and Global Extent Manager.
 */

#define _GNU_SOURCE
#include "vmm.h"
#include <sys/mman.h>
#include <stdatomic.h>
#include <unistd.h>

/* ========================================================================= *
 * Extent Configuration (VMA Fragmentation Shield)
 * ========================================================================= */

/** @brief Number of Huge Pages requested per OS syscall (32 Chunks = 64MB). */
#define LZ_EXTENT_CHUNKS 32
#define LZ_EXTENT_SIZE (LZ_HUGE_PAGE_SIZE * LZ_EXTENT_CHUNKS)

/* ========================================================================= *
 * Internal State: Global Pool & NUMA Locks
 * ========================================================================= */

/** @brief Spinlocks protecting the fast-path NUMA pools. */
static atomic_flag g_pool_locks[LZ_MAX_NUMA_NODES];

/** @brief Spinlock protecting the slow-path Global Pool. */
static atomic_flag g_global_pool_lock = ATOMIC_FLAG_INIT;

/** @brief Global Chunk Pool (Slow-path buffer linked list). */
static lz_chunk_header_t* g_global_free_chunks = NULL;

/* ========================================================================= *
 * Spinlock Helpers
 * ========================================================================= */

static LZ_ALWAYS_INLINE void pool_lock(uint32_t node_id) {
    while (atomic_flag_test_and_set_explicit(&g_pool_locks[node_id], memory_order_acquire)) {
        lz_cpu_relax();
    }
}

static LZ_ALWAYS_INLINE void pool_unlock(uint32_t node_id) {
    atomic_flag_clear_explicit(&g_pool_locks[node_id], memory_order_release);
}

static LZ_ALWAYS_INLINE void global_pool_lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_global_pool_lock, memory_order_acquire)) {
        lz_cpu_relax();
    }
}

static LZ_ALWAYS_INLINE void global_pool_unlock(void) {
    atomic_flag_clear_explicit(&g_global_pool_lock, memory_order_release);
}

/* ========================================================================= *
 * Extent Allocation (The VMA Saver)
 * ========================================================================= */

/**
 * @brief Requests a massive memory extent from the OS, aligns it, and slices it.
 * Drastically reduces `mmap` syscall frequency and prevents VMA tree fragmentation.
 */
static void sys_alloc_extent(void) {
    /* Request the Extent + 1 Huge Page margin to mathematically guarantee alignment */
    size_t request_size = LZ_EXTENT_SIZE + LZ_HUGE_PAGE_SIZE;
    
    void* raw_ptr = mmap(NULL, request_size, PROT_READ | PROT_WRITE, 
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                         
    if (LZ_UNLIKELY(raw_ptr == MAP_FAILED)) {
        return;
    }

    uintptr_t raw_addr = (uintptr_t)raw_ptr;
    uintptr_t aligned_addr = raw_addr;

    /* Trim edges only if the Kernel did not provide a natively aligned address */
    if (LZ_UNLIKELY((raw_addr & (LZ_HUGE_PAGE_SIZE - 1)) != 0)) {
        aligned_addr = LZ_ALIGN_UP(raw_addr, LZ_HUGE_PAGE_SIZE);
        size_t prefix_size = aligned_addr - raw_addr;
        size_t suffix_size = request_size - prefix_size - LZ_EXTENT_SIZE;

        if (prefix_size > 0) {
            munmap((void*)raw_addr, prefix_size);
        }
        if (suffix_size > 0) {
            munmap((void*)(aligned_addr + LZ_EXTENT_SIZE), suffix_size);
        }
    } else {
        /* Return the unused alignment margin back to the OS */
        munmap((void*)(raw_addr + LZ_EXTENT_SIZE), LZ_HUGE_PAGE_SIZE);
    }

#ifdef MADV_HUGEPAGE
    madvise((void*)aligned_addr, LZ_EXTENT_SIZE, MADV_HUGEPAGE);
#endif

    /* Slice the Extent into individual Chunks and inject them into the Global Pool */
    global_pool_lock();
    for (size_t i = 0; i < LZ_EXTENT_CHUNKS; ++i) {
        lz_chunk_header_t* chunk = (lz_chunk_header_t*)(aligned_addr + (i * LZ_HUGE_PAGE_SIZE));
        chunk->next = g_global_free_chunks;
        g_global_free_chunks = chunk;
    }
    global_pool_unlock();
}

/* ========================================================================= *
 * Public API Implementation
 * ========================================================================= */

void lz_vmm_init(void) {
    lz_topology_init();
    for (int i = 0; i < LZ_MAX_NUMA_NODES; ++i) {
        atomic_flag_clear(&g_pool_locks[i]);
    }
    atomic_flag_clear(&g_global_pool_lock);
}

lz_chunk_header_t* lz_vmm_alloc_chunk(void) {
    uint32_t current_node = lz_get_current_node();
    lz_numa_pool_t* pool = lz_topology_get_pool(current_node);
    lz_chunk_header_t* chunk = NULL;

    /* 1. FAST PATH: Attempt to steal from the local NUMA pool */
    pool_lock(current_node);
    lz_chunk_header_t* top = atomic_load_explicit(&pool->free_chunks, memory_order_relaxed);
    if (top) {
        chunk = top;
        atomic_store_explicit(&pool->free_chunks, chunk->next, memory_order_relaxed);
        atomic_fetch_sub_explicit(&pool->available_count, 1, memory_order_relaxed);
    }
    pool_unlock(current_node);

    if (LZ_LIKELY(chunk != NULL)) {
        chunk->next = NULL;
        return chunk;
    }

    /* 2. SLOW PATH: Fallback to the Global Pool */
    global_pool_lock();
    if (!g_global_free_chunks) {
        global_pool_unlock();
        sys_alloc_extent(); /* Triggers OS syscall to refill the pool */
        global_pool_lock();
    }
    
    if (g_global_free_chunks) {
        chunk = g_global_free_chunks;
        g_global_free_chunks = chunk->next;
    }
    global_pool_unlock();

    if (LZ_UNLIKELY(!chunk)) {
        return NULL; /* Hard Out-Of-Memory */
    }

    /* Neutralize chunk metadata */
    chunk->next = NULL;
    chunk->owning_tlh = NULL; 
    chunk->node_id = current_node;
    chunk->magic = LZ_CHUNK_MAGIC_V2; 
    chunk->canary = 0;
    chunk->checksum = 0;

    return chunk;
}

void lz_vmm_free_chunk(lz_chunk_header_t* chunk) {
    if (LZ_UNLIKELY(!chunk || chunk->magic != LZ_CHUNK_MAGIC_V2)) {
        return; 
    }

    uint32_t target_node = chunk->node_id;
    lz_numa_pool_t* pool = lz_topology_get_pool(target_node);
    bool return_to_global = false;

    /* 1. Thermal Hysteresis: Attempt to retain the chunk in the fast NUMA cache.
     * Prevents Soft Page Faults in high-frequency allocation workloads.
     */
    pool_lock(target_node);
    size_t count = atomic_load_explicit(&pool->available_count, memory_order_relaxed);
    if (count < LZ_VMM_MAX_CACHED_CHUNKS) {
        lz_chunk_header_t* top = atomic_load_explicit(&pool->free_chunks, memory_order_relaxed);
        chunk->next = top;
        atomic_store_explicit(&pool->free_chunks, chunk, memory_order_relaxed);
        atomic_fetch_add_explicit(&pool->available_count, 1, memory_order_relaxed);
    } else {
        /* Hot cache is saturated. This Chunk is considered cold. */
        return_to_global = true;
    }
    pool_unlock(target_node);

    /* 2. Active RSS Deflation: Return to Global Pool and purge physical memory */
    if (LZ_UNLIKELY(return_to_global)) {
        
        /* THE SAFE PAGE TECHNIQUE:
         * Calculate payload start by skipping the OS's first base page.
         * This protects the lz_chunk_header_t metadata residing in the first 64 bytes
         * while allowing the OS to reclaim the physical RAM of the actual data.
         */
        void* payload_start = (char*)chunk + LZ_PAGE_SIZE;
        size_t payload_size = LZ_HUGE_PAGE_SIZE - LZ_PAGE_SIZE;

#if defined(__linux__)
        /* Synchronous RSS deflation on Linux. Immediately visible in top/htop. */
        madvise(payload_start, payload_size, MADV_DONTNEED);
#elif defined(__APPLE__) || defined(__FreeBSD__)
        /* Gentle discarding on BSD/macOS. Reclaimed only under RAM pressure. */
        madvise(payload_start, payload_size, MADV_FREE);
#endif

        global_pool_lock();
        chunk->next = g_global_free_chunks;
        g_global_free_chunks = chunk;
        global_pool_unlock();
    }
}