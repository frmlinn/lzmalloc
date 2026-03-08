/**
 * @file lzmalloc.c
 * @brief Implementation of the POSIX API routing layer and Thread-Local state.
 */

#define _GNU_SOURCE
#include "lzmalloc.h"
#include "tlh.h"
#include "vmm.h"
#include "sizes.h"
#include "chunk.h"
#include "security.h"
#include "rtree.h"
#include "slab.h"
#include "span.h" 
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/mman.h>
#include <errno.h> 
#include <unistd.h>

/* ========================================================================= *
 * Global and Local State (TLS)
 * ========================================================================= */

static __thread lz_tlh_t* tls_tlh_ptr = NULL;
static _Atomic uint32_t g_thread_counter = 0;
static _Atomic(lz_tlh_t*) g_zombie_tlhs = NULL;
static pthread_key_t g_tlh_key;

/* ========================================================================= *
 * Internal Helpers for Large Object Path
 * ========================================================================= */

/**
 * @brief Allocates massive objects directly from the VMM, bypassing the TLH.
 * Aligns requests strictly to 2MB to integrate seamlessly with the Radix Tree.
 */
static void* lz_sys_alloc_huge_aligned(size_t total_size) {
    size_t request_size = total_size + LZ_HUGE_PAGE_SIZE;
    
    void* raw_ptr = mmap(NULL, request_size, PROT_READ | PROT_WRITE, 
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (LZ_UNLIKELY(raw_ptr == MAP_FAILED)) return NULL;

    uintptr_t raw_addr = (uintptr_t)raw_ptr;
    
    if (LZ_LIKELY((raw_addr & (LZ_HUGE_PAGE_SIZE - 1)) == 0)) {
        munmap((void*)(raw_addr + total_size), LZ_HUGE_PAGE_SIZE);
#ifdef MADV_HUGEPAGE
        madvise((void*)raw_addr, total_size, MADV_HUGEPAGE);
#endif
        return (void*)raw_addr;
    }

    uintptr_t aligned_addr = LZ_ALIGN_UP(raw_addr, LZ_HUGE_PAGE_SIZE);
    size_t prefix_size = aligned_addr - raw_addr;
    size_t suffix_size = request_size - prefix_size - total_size;

    if (prefix_size > 0) munmap((void*)raw_addr, prefix_size);
    if (suffix_size > 0) munmap((void*)(aligned_addr + total_size), suffix_size);

#ifdef MADV_HUGEPAGE
    madvise((void*)aligned_addr, total_size, MADV_HUGEPAGE);
#endif
    return (void*)aligned_addr;
}

/* ========================================================================= *
 * Resurrection and Garbage Collection (Reaper)
 * ========================================================================= */

static lz_tlh_t* lz_resurrect_zombie(void) {
    lz_tlh_t* head = atomic_load_explicit(&g_zombie_tlhs, memory_order_acquire);
    while (head != NULL) {
        if (atomic_compare_exchange_weak_explicit(&g_zombie_tlhs, 
                                                  &head, head->next_zombie,
                                                  memory_order_acquire, 
                                                  memory_order_relaxed)) {
            head->next_zombie = NULL;
            return head;
        }
    }
    return NULL; 
}

void lzmalloc_gc(void) {
    lz_tlh_t* current = atomic_load_explicit(&g_zombie_tlhs, memory_order_acquire);
    while (current != NULL) {
        lz_tlh_reap(current); /* Sweeps remote frees into the correct engine */
        current = current->next_zombie;
    }
}

/* ========================================================================= *
 * Lifecycle and Initialization
 * ========================================================================= */

static void lz_tlh_destructor(void* arg) {
    lz_tlh_t* tlh = (lz_tlh_t*)arg;
    if (LZ_UNLIKELY(!tlh)) return;

    lz_tlh_flush_outgoing_batch(tlh);
    
    if (tlh->stat_slot) {
        if (tlh->local_bytes_alloc_batch > 0) {
            atomic_fetch_add_explicit(&tlh->stat_slot->bytes_allocated, tlh->local_bytes_alloc_batch, memory_order_relaxed);
        }
        if (tlh->local_bytes_free_batch > 0) {
            atomic_fetch_sub_explicit(&tlh->stat_slot->bytes_allocated, tlh->local_bytes_free_batch, memory_order_relaxed);
        }
        tlh->local_bytes_alloc_batch = 0;
        tlh->local_bytes_free_batch = 0;
    }

    tlh->is_zombie = 1;

    lz_tlh_t* old_head = atomic_load_explicit(&g_zombie_tlhs, memory_order_relaxed);
    do {
        tlh->next_zombie = old_head;
    } while (!atomic_compare_exchange_weak_explicit(&g_zombie_tlhs, 
                                                    &old_head, tlh,
                                                    memory_order_release, 
                                                    memory_order_relaxed));
}

void lz_system_init(void) {
    lz_security_init(); 
    lz_vmm_init();      
    lz_rtree_init();    
    pthread_key_create(&g_tlh_key, lz_tlh_destructor);
    if (getenv("LZMALLOC_TELEMETRY") != NULL) {
        lz_telemetry_init();
    }
}

static LZ_ALWAYS_INLINE void ensure_tls_init(void) {
    if (LZ_LIKELY(tls_tlh_ptr != NULL)) return;

    tls_tlh_ptr = lz_resurrect_zombie();

    if (tls_tlh_ptr != NULL) {
        tls_tlh_ptr->is_zombie = 0;
        tls_tlh_ptr->thread_id = atomic_fetch_add_explicit(&g_thread_counter, 1, memory_order_relaxed);
    } else {
        tls_tlh_ptr = (lz_tlh_t*)mmap(NULL, sizeof(lz_tlh_t), 
                                      PROT_READ | PROT_WRITE, 
                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (LZ_UNLIKELY(tls_tlh_ptr == MAP_FAILED)) __builtin_trap(); 

        uint32_t tid = atomic_fetch_add_explicit(&g_thread_counter, 1, memory_order_relaxed);
        lz_tlh_init(tls_tlh_ptr, tid);
        tls_tlh_ptr->is_zombie = 0;
        tls_tlh_ptr->next_zombie = NULL;
    }

    tls_tlh_ptr->local_bytes_alloc_batch = 0;
    tls_tlh_ptr->local_bytes_free_batch = 0;
    uint32_t max_thread_slots = LZ_MAX_TELEMETRY_SLOTS - LZ_THREAD_SLOT_BASE_IDX;
    uint32_t safe_idx = tls_tlh_ptr->thread_id % max_thread_slots;
    tls_tlh_ptr->stat_slot = lz_telemetry_get_slot(LZ_THREAD_SLOT_BASE_IDX + safe_idx);
    
    pthread_setspecific(g_tlh_key, tls_tlh_ptr);
}

/* ========================================================================= *
 * POSIX API Implementation (The Master Router)
 * ========================================================================= */

void* lz_malloc(size_t size) {
    if (LZ_UNLIKELY(size == 0)) size = 1;

    /* 1. FAST PATH: Slabs (Objects <= 32KB) */
    if (LZ_LIKELY(size <= LZ_MAX_SLAB_OBJ_SIZE)) {
        ensure_tls_init();
        return lz_tlh_alloc(tls_tlh_ptr, size);
    } 
    /* 2. WARM PATH: Binned Spans (Objects up to 1MB) */
    else if (size <= 1048576) {
        ensure_tls_init();
        return lz_span_alloc(tls_tlh_ptr, size);
    }
    /* 3. SLOW PATH: Direct VMM mmap for Huge Objects */
    else {
        size_t payload_offset = 128; 
        size_t required_bytes = payload_offset + size;
        size_t total_bytes = LZ_ALIGN_UP(required_bytes, LZ_HUGE_PAGE_SIZE);
        
        void* ptr = lz_sys_alloc_huge_aligned(total_bytes);
        if (LZ_UNLIKELY(!ptr)) return NULL;
        
        lz_chunk_header_t* header = (lz_chunk_header_t*)ptr;
        header->magic = LZ_CHUNK_MAGIC_V2;
        header->owning_tlh = NULL; 
        header->chunk_type = LZ_CHUNK_TYPE_DIRECT;
        header->checksum = lz_calc_checksum(header);
        
        /* Store allocation footprint at Cache Line 2 */
        *((size_t*)((char*)ptr + LZ_CACHE_LINE_SIZE)) = total_bytes; 
        *((size_t*)((char*)ptr + LZ_CACHE_LINE_SIZE + sizeof(size_t))) = size;
        
        void* user_ptr = (void*)((char*)ptr + payload_offset);
        
        /* Register EVERY 2MB window in the Radix Tree */
        uintptr_t base_addr = (uintptr_t)ptr;
        for (size_t offset = 0; offset < total_bytes; offset += LZ_HUGE_PAGE_SIZE) {
            lz_rtree_set(base_addr + offset, header);
        }
        
        return user_ptr;
    }
}

void lz_free(void* ptr) {
    if (LZ_UNLIKELY(!ptr)) return;

    /* O(1) Lookup: Who owns this memory? */
    lz_chunk_header_t* chunk = lz_rtree_get(ptr);

    if (!chunk) {
        uintptr_t chunk_mask = ~(((uintptr_t)1 << LZ_CHUNK_SHIFT) - 1);
        uintptr_t chunk_base = (uintptr_t)ptr & chunk_mask;
        chunk = lz_rtree_get((void*)chunk_base);
        if (!chunk) return;
    }

    if (LZ_UNLIKELY(chunk->magic != LZ_CHUNK_MAGIC_V2)) return;

    /* Direct Mmap Resolution */
    if (LZ_UNLIKELY(chunk->chunk_type == LZ_CHUNK_TYPE_DIRECT)) {
        size_t total_bytes = *((size_t*)((char*)chunk + LZ_CACHE_LINE_SIZE));
        uintptr_t base_addr = (uintptr_t)chunk;
        
        for (size_t offset = 0; offset < total_bytes; offset += LZ_HUGE_PAGE_SIZE) {
            lz_rtree_clear(base_addr + offset);
        }
        
        munmap(chunk, total_bytes);
        return;
    }

    /* Route to TLH (Which internally discriminates Slabs vs Spans) */
    ensure_tls_init();
    lz_tlh_free(tls_tlh_ptr, ptr); 
}

void* lz_calloc(size_t num, size_t size) {
    if (LZ_UNLIKELY(num == 0 || size == 0)) {
        num = 1; size = 1;
    }
    if (LZ_UNLIKELY(SIZE_MAX / num < size)) return NULL; 

    size_t total_size = num * size;
    void* ptr = lz_malloc(total_size);
    if (LZ_UNLIKELY(!ptr)) return NULL;

    /* Only memset if memory came from our cached pools (Slabs/Spans). 
     * Direct Mmap > 1MB is already guaranteed zeroed by the OS kernel. */
    if (LZ_LIKELY(total_size <= 1048576)) {
        memset(ptr, 0, total_size);
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
    if (LZ_UNLIKELY(old_size == 0)) return NULL; 

    if (new_size <= old_size) return ptr;

    void* new_ptr = lz_malloc(new_size);
    if (LZ_UNLIKELY(!new_ptr)) return NULL; 

    memcpy(new_ptr, ptr, old_size);
    lz_free(ptr); 
    
    return new_ptr;
}

static inline int is_power_of_two(size_t x) {
    return (x != 0) && ((x & (x - 1)) == 0);
}

void* lz_memalign(size_t alignment, size_t size) {
    if (LZ_UNLIKELY(alignment == 0 || !is_power_of_two(alignment))) return NULL;

    /* PATH 1: Slabs (Natively cache-line aligned up to 64 bytes) */
    if (alignment <= 64 && size <= LZ_MAX_SLAB_OBJ_SIZE) {
        size_t promoted_size = LZ_ALIGN_UP(size, alignment);
        return lz_malloc(promoted_size);
    }

    /* PATH 2: Spans (Natively OS Page aligned to 4096 bytes) */
    if (alignment <= LZ_PAGE_SIZE && size <= 1048576) {
        ensure_tls_init();
        return lz_span_alloc(tls_tlh_ptr, size);
    }

    /* PATH 3: Direct mmap for extreme alignment or massive sizes */
    size_t base_offset = 128; 
    size_t dynamic_offset = LZ_ALIGN_UP(base_offset, alignment);
    
    size_t required_bytes = dynamic_offset + size;
    size_t total_bytes = LZ_ALIGN_UP(required_bytes, LZ_HUGE_PAGE_SIZE);
    
    void* ptr = lz_sys_alloc_huge_aligned(total_bytes);
    if (LZ_UNLIKELY(!ptr)) return NULL;
    
    lz_chunk_header_t* header = (lz_chunk_header_t*)ptr;
    header->magic = LZ_CHUNK_MAGIC_V2;
    header->owning_tlh = NULL; 
    header->chunk_type = LZ_CHUNK_TYPE_DIRECT;
    header->checksum = lz_calc_checksum(header);
    
    *((size_t*)((char*)ptr + LZ_CACHE_LINE_SIZE)) = total_bytes; 
    *((size_t*)((char*)ptr + LZ_CACHE_LINE_SIZE + sizeof(size_t))) = size;
    
    void* user_ptr = (void*)((char*)ptr + dynamic_offset);
    
    uintptr_t base_addr = (uintptr_t)ptr;
    for (size_t offset = 0; offset < total_bytes; offset += LZ_HUGE_PAGE_SIZE) {
        lz_rtree_set(base_addr + offset, header);
    }
    
    return user_ptr;
}

int lz_posix_memalign(void **memptr, size_t alignment, size_t size) {
    if (LZ_UNLIKELY(memptr == NULL)) return EINVAL;
    if (LZ_UNLIKELY(alignment % sizeof(void*) != 0 || !is_power_of_two(alignment))) {
        return EINVAL;
    }

    void* ptr = lz_memalign(alignment, size);
    if (LZ_UNLIKELY(!ptr)) return ENOMEM;

    *memptr = ptr;
    return 0;
}

void* lz_aligned_alloc(size_t alignment, size_t size) {
    if (LZ_UNLIKELY(alignment == 0 || !is_power_of_two(alignment))) return NULL;
    if (LZ_UNLIKELY(size % alignment != 0)) return NULL;
    return lz_memalign(alignment, size);
}

void* lz_valloc(size_t size) {
    long page_size = sysconf(_SC_PAGESIZE);
    if (LZ_UNLIKELY(page_size <= 0)) page_size = 4096; 
    return lz_memalign((size_t)page_size, size);
}

void* lz_pvalloc(size_t size) {
    long page_size = sysconf(_SC_PAGESIZE);
    if (LZ_UNLIKELY(page_size <= 0)) page_size = 4096;
    size_t rounded_size = LZ_ALIGN_UP(size, (size_t)page_size);
    return lz_memalign((size_t)page_size, rounded_size);
}

/* ========================================================================= *
 * POSIX API Extension (Metadata Reflection)
 * ========================================================================= */

size_t lz_malloc_usable_size(void* ptr) {
    if (LZ_UNLIKELY(!ptr)) return 0;

    lz_chunk_header_t* chunk = lz_rtree_get(ptr);
    if (!chunk) {
        uintptr_t chunk_mask = ~(((uintptr_t)1 << LZ_CHUNK_SHIFT) - 1);
        uintptr_t chunk_base = (uintptr_t)ptr & chunk_mask;
        chunk = lz_rtree_get((void*)chunk_base);
        if (!chunk) return 0; 
    }

    if (LZ_UNLIKELY(chunk->magic != LZ_CHUNK_MAGIC_V2)) return 0;

    if (chunk->chunk_type == LZ_CHUNK_TYPE_DIRECT) {
        /* Read exact requested user size from Cache Line 2 */
        return *((size_t*)((char*)chunk + LZ_CACHE_LINE_SIZE + sizeof(size_t)));
    } 
    else if (chunk->chunk_type == LZ_CHUNK_TYPE_SLAB) {
        lz_slab_t* slab = (lz_slab_t*)((char*)chunk + LZ_CACHE_LINE_SIZE);
        return slab->block_size;
    } 
    else if (chunk->chunk_type == LZ_CHUNK_TYPE_SPAN) {
        lz_span_t* span = (lz_span_t*)((char*)chunk + LZ_CACHE_LINE_SIZE);
        uintptr_t offset = (uintptr_t)ptr - (uintptr_t)chunk;
        uint32_t start_page = (uint32_t)(offset / LZ_PAGE_SIZE);
        return (size_t)(span->alloc_size_pages[start_page]) * LZ_PAGE_SIZE;
    }

    return 0;
}