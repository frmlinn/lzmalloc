/**
 * @file api.c
 * @brief Implementación interna de lzmalloc.
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

extern void lz_vmm_init(void);

void lz_system_init(void) {
    LZ_INFO("Inicializando lzmalloc v0.2.0 (Motores RSEQ, Fast-Paths O(1)).");
    lz_cpu_rt_thread_init();
    lz_security_init();
    lz_pagemap_init();
    lz_vmm_init();
}

void* lz_malloc(size_t size) {
    uint32_t core_id = lz_cpu_get_core_id();
    lz_core_heap_t* heap = &g_core_heaps[core_id];

    if (LZ_UNLIKELY(lz_atomic_load_acquire(&heap->remote_free_mailbox) != 0)) {
        lz_core_heap_reap_mailbox(heap);
    }

    if (LZ_LIKELY(size <= LZ_MAX_SLAB_SIZE)) {
        uint32_t idx, block_size;
        lz_routing_get_size_class(size, &idx, &block_size);
        return lz_slab_alloc_local(heap, idx, block_size);
    }

    if (LZ_LIKELY(size <= LZ_MAX_SPAN_SIZE)) {
        size_t exact_size = (size + LZ_PAGE_SIZE - 1) & ~(LZ_PAGE_SIZE - 1);
        return lz_span_alloc_local(heap, 0, exact_size);
    }

    /* Direct OS */
    size_t huge_size = (size + LZ_PAGE_SIZE - 1) & ~(LZ_PAGE_SIZE - 1);
    void* ptr = lz_os_alloc_aligned(huge_size, LZ_PAGE_SIZE);
    
    if (LZ_LIKELY(ptr)) {
        lz_pagemap_set_slow(ptr, NULL); 
        LZ_DEBUG("Direct OS Alloc: %zu bytes en %p", huge_size, ptr);
    }
    return ptr;
}

void lz_free(void* ptr) {
    if (LZ_UNLIKELY(!ptr)) return;

    lz_chunk_t* chunk = lz_meta_resolve(ptr);

    if (LZ_UNLIKELY(!chunk)) {
        LZ_WARN("Intento de liberar Huge Allocation (Direct OS) en %p sin VMA tracker. ¡Posible fuga de memoria virtual!", ptr);
        return; 
    }

    uint32_t current_core = lz_cpu_get_core_id();

    if (LZ_LIKELY(chunk->core_id == current_core)) {
        lz_core_heap_t* heap = &g_core_heaps[current_core];
        if (chunk->chunk_type == LZ_CHUNK_TYPE_SLAB) {
            lz_slab_free_local(heap, chunk, ptr);
        } else {
            lz_span_free_local(heap, chunk, ptr);
        }
    } else {
        lz_core_heap_remote_free(chunk->core_id, ptr);
    }
}

void* lz_calloc(size_t num, size_t size) {
    size_t total;
    if (LZ_UNLIKELY(__builtin_mul_overflow(num, size, &total))) {
        return NULL; 
    }
    
    void* ptr = lz_malloc(total);
    if (LZ_LIKELY(ptr)) {
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
    if (alignment <= 8) return lz_malloc(size);
    size_t request = size > alignment ? size : alignment;
    request = (request + alignment - 1) & ~(alignment - 1);
    return lz_malloc(request);
}

void* lz_valloc(size_t size) {
    return lz_memalign(LZ_PAGE_SIZE, size);
}

void* lz_pvalloc(size_t size) {
    size_t rounded_size = (size + LZ_PAGE_SIZE - 1) & ~(LZ_PAGE_SIZE - 1);
    return lz_memalign(LZ_PAGE_SIZE, rounded_size);
}