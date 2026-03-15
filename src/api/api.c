/**
 * @file api.c
 * @brief Internal Implementation of lzmalloc v0.2.0 via Arena Multiplexing.
 * @details This module serves as the primary dispatcher for the lzmalloc engine. 
 * It implements a "Wait-Free Try-Lock" strategy with Fallback Arenas to solve the 
 * "Single-Core Deadlock" problem inherent in user-space RSEQ (Restartable Sequences).
 * By detecting preemption at the atomic level, it ensures that threads displaced 
 * from their preferred core can still allocate memory with O(1) latency using 
 * auxiliary heaps.
 */

#include "lzmalloc.h"
#include "routing.h"
#include "cpu_rt.h"
#include "core_heap.h"
#include "pagemap.h"
#include "slab.h"
#include "span_bin.h"
#include "memory.h"
#include "security.h"
#include "atomics.h"
#include "lz_log.h"
#include <string.h>
#include <errno.h>
#include <pthread.h> 

/* External reference to the Virtual Memory Manager initializer */
extern void lz_vmm_init(void);

/* ========================================================================= *
 * INTERNAL SYNCHRONIZATION PRIMITIVES
 * ========================================================================= */

/**
 * @brief Acquires a spin-lock for Fallback Arenas.
 * @details Fallback heaps are shared among multiple threads, unlike core-local heaps. 
 * Therefore, they require traditional exclusion.
 * @param heap Pointer to the fallback heap instance.
 * @note Emits lz_cpu_relax() to signal the processor's BTB and reduce power 
 * consumption during the contention loop.
 */
static LZ_ALWAYS_INLINE void lock_fallback(lz_core_heap_t* heap) {
    uint8_t expected;
    do {
        expected = 0;
        /* Atomic CAS-weak for fast acquisition in low-contention scenarios */
        if (lz_atomic_cas_weak(&heap->fallback_lock, &expected, 1)) break;
        lz_cpu_relax(); 
    } while (1);
}

/**
 * @brief Releases the spin-lock for Fallback Arenas.
 * @param heap Pointer to the fallback heap instance.
 * @note Uses Release semantics to ensure memory visibility of preceding writes.
 */
static LZ_ALWAYS_INLINE void unlock_fallback(lz_core_heap_t* heap) {
    lz_atomic_store_release(&heap->fallback_lock, 0);
}

/* ========================================================================= *
 * PUBLIC SYSTEM API
 * ========================================================================= */

void lz_system_init(void) {
    LZ_INFO("Initializing lzmalloc v0.2.0 (RSEQ-ready, Wait-Free Multiplexing).");
    lz_security_init();  /* Initialize CSPRNG for Safe Linking */
    lz_pagemap_init();   /* Reserve 512GB Sparse Virtual Array */
    lz_vmm_init();       /* Bootstrap 1TB Virtual Arena */
}

/**
 * @brief High-Performance Allocation Entry Point.
 * @details Implements a 3-tier routing logic:
 * 1. Slab Engine: Small objects (<= 32KB).
 * 2. Span Engine: Medium objects (<= 1MB).
 * 3. Direct OS: Large objects (> 1MB).
 * * @param size Requested allocation size in bytes.
 * @return Virtual address of the allocated block, or NULL on failure.
 */
void* lz_malloc(size_t size) {
    /* Per-thread bootstrapping to ensure the RSEQ ABI is registered */
    static __thread uint32_t is_thread_ready __attribute__((tls_model("initial-exec"))) = 0;
    if (LZ_UNLIKELY(is_thread_ready == 0)) {
        lz_cpu_rt_thread_init();
        is_thread_ready = 1;
    }

    /* 1. TOPOLOGICAL AFFINITY RESOLUTION */
    uint32_t core_id = lz_cpu_get_core_id();
    lz_core_heap_t* heap = &g_core_heaps[core_id];
    
    uint8_t expected = 0;
    bool is_fallback = false;

    /* 2. CORE-LOCAL TRY-LOCK (PREEMPTION DETECTION) */
    if (LZ_UNLIKELY(!lz_atomic_cas_weak(&heap->is_busy, &expected, 1))) {
        /**
         * @details PREEMPTION DETECTED: If the core-local heap is busy, it means 
         * the current thread was likely rescheduled or interrupted. To avoid 
         * deadlocking on a busy core-local resource, we hash the Thread ID 
         * to select a Fallback Arena.
         */
        uint32_t hash = (uint32_t)((uintptr_t)pthread_self() >> 12);
        uint32_t fallback_idx = LZ_MAX_CORES + (hash % LZ_FALLBACK_HEAPS);
        
        heap = &g_core_heaps[fallback_idx];
        lock_fallback(heap);
        is_fallback = true;
        
        /* Lazy initialization for fallback heaps */
        if (LZ_UNLIKELY(!heap->is_initialized)) {
            heap->core_id = fallback_idx;
            heap->is_initialized = 1;
        }
    }

    /* 3. ASYNCHRONOUS MAILBOX SYNC */
    /* Drain remote deallocations from other cores before allocating new space */
    if (LZ_UNLIKELY(lz_atomic_load_acquire(&heap->remote_free_mailbox) != 0)) {
        lz_core_heap_reap_mailbox(heap);
    }

    void* ptr = NULL;
    
    /* 4. ENGINE ROUTING */
    if (LZ_LIKELY(size <= LZ_MAX_SLAB_SIZE)) {
        /* Binned Small Object path (Fast-path) */
        uint32_t idx, block_size;
        lz_routing_get_size_class(size, &idx, &block_size);
        ptr = lz_slab_alloc_local(heap, idx, block_size);
    }
    else if (LZ_LIKELY(size <= LZ_MAX_SPAN_SIZE)) {
        /* Segregated Span path (Medium-path) */
        size_t exact_size = (size + LZ_PAGE_SIZE - 1) & ~(LZ_PAGE_SIZE - 1);
        ptr = lz_span_alloc_local(heap, 0, exact_size);
    }
    else {
        /* Direct Kernel mapping (Slow-path) */
        size_t huge_size = (size + LZ_PAGE_SIZE - 1) & ~(LZ_PAGE_SIZE - 1);
        ptr = lz_os_alloc_aligned(huge_size, LZ_PAGE_SIZE);
        if (LZ_LIKELY(ptr)) lz_pagemap_set_slow(ptr, NULL); 
    }

    /* 5. LOCK DEALLOCATION */
    if (LZ_UNLIKELY(is_fallback)) {
        unlock_fallback(heap);
    } else {
        lz_atomic_store_release(&heap->is_busy, 0);
    }

    return ptr;
}

/**
 * @brief High-Performance Deallocation Entry Point.
 * @details Implements a context-aware free logic:
 * - If the block belongs to a fallback heap, it locks and frees locally.
 * - If the block belongs to the current core and is not busy, it frees locally.
 * - Otherwise, it performs an asynchronous "Remote Free" by posting the 
 * pointer to the originating core's mailbox.
 * * @param ptr Pointer to the memory block to be released.
 */
void lz_free(void* ptr) {
    if (LZ_UNLIKELY(!ptr)) return;

    /* Resolve metadata using the O(1) Flat Pagemap */
    lz_chunk_t* chunk = lz_meta_resolve(ptr);
    if (LZ_UNLIKELY(!chunk)) return; 

    uint32_t current_core = lz_cpu_get_core_id();
    uint32_t chunk_core = chunk->core_id;

    /* SCENARIO A: Block belongs to a Fallback Arena */
    if (LZ_UNLIKELY(chunk_core >= LZ_MAX_CORES)) {
        lz_core_heap_t* heap = &g_core_heaps[chunk_core];
        lock_fallback(heap);
        if (chunk->chunk_type == LZ_CHUNK_TYPE_SLAB) lz_slab_free_local(heap, chunk, ptr);
        else lz_span_free_local(heap, chunk, ptr);
        unlock_fallback(heap);
        return;
    }

    /* SCENARIO B: Block belongs to a physical Core Heap */
    if (LZ_LIKELY(chunk_core == current_core)) {
        lz_core_heap_t* heap = &g_core_heaps[current_core];
        uint8_t expected = 0;
        
        /* Attempt to acquire the core-local heap to free locally */
        if (LZ_LIKELY(lz_atomic_cas_weak(&heap->is_busy, &expected, 1))) {
            if (chunk->chunk_type == LZ_CHUNK_TYPE_SLAB) lz_slab_free_local(heap, chunk, ptr);
            else lz_span_free_local(heap, chunk, ptr);
            
            lz_atomic_store_release(&heap->is_busy, 0);
            return;
        }
        /**
         * @note If the try-lock fails, it implies reentrancy (e.g., free called 
         * within a signal handler) or preemption. We fall through to the 
         * asynchronous Remote Free path to maintain safety.
         */
    }
    
    /* SCENARIO C: Asynchronous Remote Free (MPSC Mailbox) */
    lz_core_heap_remote_free(chunk_core, ptr);
}

/* ========================================================================= *
 * POSIX / C11 COMPATIBILITY LAYER
 * ========================================================================= */

void* lz_calloc(size_t num, size_t size) {
    size_t total;
    /* Use compiler intrinsic for safe overflow detection */
    if (LZ_UNLIKELY(__builtin_mul_overflow(num, size, &total))) {
        return NULL; 
    }
    
    void* ptr = lz_malloc(total);
    if (LZ_LIKELY(ptr)) {
        /* Optimized clearing of the allocated range */
        memset(ptr, 0, total);
    }
    return ptr;
}

void* lz_realloc(void* ptr, size_t new_size) {
    if (LZ_UNLIKELY(!ptr)) return lz_malloc(new_size);
    if (LZ_UNLIKELY(new_size == 0)) {
        lz_free(ptr);
        return NULL;
    }

    size_t old_size = lz_malloc_usable_size(ptr);
    
    /* Optimization: If the current block footprint satisfies the new request, 
     * return immediately (In-place realloc). */
    if (LZ_LIKELY(new_size <= old_size)) {
        return ptr; 
    }

    void* new_ptr = lz_malloc(new_size);
    if (LZ_LIKELY(new_ptr)) {
        memcpy(new_ptr, ptr, old_size);
        lz_free(ptr);
    }
    
    return new_ptr;
}

int lz_posix_memalign(void **memptr, size_t alignment, size_t size) {
    /* POSIX safety check: alignment must be a power of 2 and multiple of sizeof(void*) */
    if (LZ_UNLIKELY(alignment % sizeof(void*) != 0 || (alignment & (alignment - 1)) != 0)) {
        return EINVAL; 
    }

    void* ptr = lz_memalign(alignment, size);
    if (LZ_UNLIKELY(!ptr)) {
        return ENOMEM; 
    }

    *memptr = ptr;
    return 0;
}

size_t lz_malloc_usable_size(void* ptr) {
    if (LZ_UNLIKELY(!ptr)) return 0;

    lz_chunk_t* chunk = lz_meta_resolve(ptr);
    if (LZ_UNLIKELY(!chunk)) return 0; 

    /* Determine the engine and return the exact block footprint */
    if (chunk->chunk_type == LZ_CHUNK_TYPE_SLAB) {
        lz_slab_bin_t* slab = (lz_slab_bin_t*)((uint8_t*)chunk + LZ_CACHE_LINE_SIZE);
        return slab->block_size;
    } 
    else if (chunk->chunk_type == LZ_CHUNK_TYPE_SPAN) {
        lz_span_bin_t* bin = (lz_span_bin_t*)((uint8_t*)chunk + LZ_CACHE_LINE_SIZE);
        return bin->span_size;
    }

    return 0;
}

void* lz_memalign(size_t alignment, size_t size) {
    /** * @details Logic: 
     * 1. If alignment <= 128, the Slab Engine naturally satisfies it because 
     * block_sizes are multiples of 8 and metadata is aligned. 
     * 2. If alignment > 128 (e.g., Page Alignment), we force the allocation 
     * through the Direct OS engine to use mmap's natural page boundaries.
     */
    if (alignment <= 128) {
        size_t request = size > alignment ? size : alignment;
        return lz_malloc(request);
    }

    size_t huge_size = (size + LZ_PAGE_SIZE - 1) & ~(LZ_PAGE_SIZE - 1);
    size_t final_align = alignment > LZ_PAGE_SIZE ? alignment : LZ_PAGE_SIZE;
    
    void* ptr = lz_os_alloc_aligned(huge_size, final_align);
    if (LZ_LIKELY(ptr)) {
        lz_pagemap_set_slow(ptr, NULL); 
    }
    return ptr;
}

void* lz_valloc(size_t size) {
    return lz_memalign(LZ_PAGE_SIZE, size);
}

void* lz_pvalloc(size_t size) {
    size_t rounded_size = (size + LZ_PAGE_SIZE - 1) & ~(LZ_PAGE_SIZE - 1);
    return lz_memalign(LZ_PAGE_SIZE, rounded_size);
}