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

#define LZ_EXTENT_CHUNKS 32
#define LZ_EXTENT_SIZE (LZ_HUGE_PAGE_SIZE * LZ_EXTENT_CHUNKS)

/* ========================================================================= *
 * Internal State: Global Pool & Protected NUMA Locks
 * ========================================================================= */

/**
 * @struct lz_pool_lock_t
 * @brief Padding wrapper to prevent catastrophic False Sharing of NUMA locks.
 */
typedef struct LZ_CACHE_ALIGNED {
    atomic_flag lock;
} lz_pool_lock_t;

/** @brief Spinlocks mathematically isolated across CPU caches. */
static lz_pool_lock_t g_pool_locks[LZ_MAX_NUMA_NODES];

/** @brief Spinlock protecting the slow-path Global Pool. */
static atomic_flag g_global_pool_lock = ATOMIC_FLAG_INIT;

/** @brief Global Chunk Pool (Slow-path buffer linked list). */
static lz_chunk_header_t* g_global_free_chunks = NULL;

/* ========================================================================= *
 * Spinlock Helpers
 * ========================================================================= */

static LZ_ALWAYS_INLINE void pool_lock(uint32_t node_id) {
    while (atomic_flag_test_and_set_explicit(&g_pool_locks[node_id].lock, memory_order_acquire)) {
        lz_cpu_relax();
    }
}

static LZ_ALWAYS_INLINE void pool_unlock(uint32_t node_id) {
    atomic_flag_clear_explicit(&g_pool_locks[node_id].lock, memory_order_release);
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

static void sys_alloc_extent(void) {
    size_t request_size = LZ_EXTENT_SIZE + LZ_HUGE_PAGE_SIZE;
    
    void* raw_ptr = mmap(NULL, request_size, PROT_READ | PROT_WRITE, 
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                         
    if (LZ_UNLIKELY(raw_ptr == MAP_FAILED)) {
        return;
    }

    uintptr_t raw_addr = (uintptr_t)raw_ptr;
    uintptr_t aligned_addr = raw_addr;

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
        munmap((void*)(raw_addr + LZ_EXTENT_SIZE), LZ_HUGE_PAGE_SIZE);
    }

#ifdef MADV_HUGEPAGE
    madvise((void*)aligned_addr, LZ_EXTENT_SIZE, MADV_HUGEPAGE);
#endif

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
        /* Standard C11 strictly dictates proper initialization for atomic flags */
        atomic_flag_clear_explicit(&g_pool_locks[i].lock, memory_order_release);
    }
    atomic_flag_clear_explicit(&g_global_pool_lock, memory_order_release);
}

lz_chunk_header_t* lz_vmm_alloc_chunk(void) {
    uint32_t current_node = lz_get_current_node();
    lz_numa_pool_t* pool = lz_topology_get_pool(current_node);
    lz_chunk_header_t* chunk = NULL;

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

    global_pool_lock();
    if (!g_global_free_chunks) {
        global_pool_unlock();
        sys_alloc_extent(); 
        global_pool_lock();
    }
    
    if (g_global_free_chunks) {
        chunk = g_global_free_chunks;
        g_global_free_chunks = chunk->next;
    }
    global_pool_unlock();

    if (LZ_UNLIKELY(!chunk)) {
        return NULL; 
    }

    /* Strict Neutralization: Erase any historical entropy */
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

    pool_lock(target_node);
    size_t count = atomic_load_explicit(&pool->available_count, memory_order_relaxed);
    if (count < LZ_VMM_MAX_CACHED_CHUNKS) {
        lz_chunk_header_t* top = atomic_load_explicit(&pool->free_chunks, memory_order_relaxed);
        chunk->next = top;
        atomic_store_explicit(&pool->free_chunks, chunk, memory_order_relaxed);
        atomic_fetch_add_explicit(&pool->available_count, 1, memory_order_relaxed);
    } else {
        return_to_global = true;
    }
    pool_unlock(target_node);

    if (LZ_UNLIKELY(return_to_global)) {
        /* SAFE PAGE TECHNIQUE: Physical page release skipping the metadata boundary */
        void* payload_start = (char*)chunk + LZ_PAGE_SIZE;
        size_t payload_size = LZ_HUGE_PAGE_SIZE - LZ_PAGE_SIZE;

#if defined(__linux__)
        madvise(payload_start, payload_size, MADV_DONTNEED);
#elif defined(__APPLE__) || defined(__FreeBSD__)
        madvise(payload_start, payload_size, MADV_FREE);
#endif

        global_pool_lock();
        chunk->next = g_global_free_chunks;
        g_global_free_chunks = chunk;
        global_pool_unlock();
    }
}

/* ========================================================================= *
 * Active RSS Deflation (Garbage Collection)
 * ========================================================================= */

void lz_vmm_purge_all_caches(void) {
    for (uint32_t i = 0; i < LZ_MAX_NUMA_NODES; ++i) {
        lz_numa_pool_t* pool = lz_topology_get_pool(i);
        
        /* 1. Lock and drain the entire NUMA cache */
        pool_lock(i);
        lz_chunk_header_t* chunk = atomic_exchange_explicit(&pool->free_chunks, NULL, memory_order_relaxed);
        atomic_store_explicit(&pool->available_count, 0, memory_order_relaxed);
        pool_unlock(i);

        /* 2. Deflate physical memory and route to the Global Pool */
        while (chunk) {
            lz_chunk_header_t* next = chunk->next;
            
            /* Safe boundary calculation to preserve the 64-byte metadata header */
            void* payload_start = (char*)chunk + LZ_PAGE_SIZE;
            size_t payload_size = LZ_HUGE_PAGE_SIZE - LZ_PAGE_SIZE;

#if defined(__linux__)
            madvise(payload_start, payload_size, MADV_DONTNEED);
#elif defined(__APPLE__) || defined(__FreeBSD__)
            madvise(payload_start, payload_size, MADV_FREE);
#endif

            /* Push the deflated chunk into the global pool */
            global_pool_lock();
            chunk->next = g_global_free_chunks;
            g_global_free_chunks = chunk;
            global_pool_unlock();

            chunk = next;
        }
    }
}