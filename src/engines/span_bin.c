/**
 * @file span_bin.c
 * @brief Implementation of the Segregated Span Engine for Medium Objects (32KB - 1MB).
 * @details Utilizes a 64-bit bitmap and bit-leaping intrinsics (__builtin_ctzll) 
 * for O(1) allocation and free operations. Spans are segregated by size 
 * class to eliminate internal fragmentation.
 */
#include "span_bin.h"
#include "vmm.h"
#include "pagemap.h"
#include "lz_log.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Allocates a medium object from core-local span bins.
 * @details Performs a linear search of the active bin list (usually very short) 
 * and then executes a single-cycle bit-scan to find a free segment.
 * @param heap Current core-local heap.
 * @param size_class_idx Reserved for future hierarchical routing.
 * @param exact_size The page-aligned size of the requested block.
 * @return Virtual pointer to the allocated span or NULL.
 */
void* lz_span_alloc_local(lz_core_heap_t* heap, uint32_t size_class_idx, size_t exact_size) {
    (void)size_class_idx;
    lz_span_bin_t* bin = heap->active_spans;

    /* 1. Linear Scan for a partial bin matching the exact size requirement */
    while (LZ_LIKELY(bin != NULL)) {
        if (bin->span_size == exact_size && bin->used_spans < bin->max_spans) {
            break;
        }
        bin = bin->next;
    }

    /* 2. Slow-Path: Provision a new 2MB chunk if no partial bins exist */
    if (LZ_UNLIKELY(bin == NULL)) {
        lz_chunk_t* chunk = lz_vmm_alloc_chunk(heap->core_id);
        if (LZ_UNLIKELY(!chunk)) return NULL;

        chunk->chunk_type = LZ_CHUNK_TYPE_SPAN;
        lz_pagemap_set_slow(chunk, chunk);

        bin = (lz_span_bin_t*)((uint8_t*)chunk + LZ_CACHE_LINE_SIZE);
        bin->span_size = exact_size;
        
        uint32_t available_bytes = LZ_HUGE_PAGE_SIZE - (LZ_CACHE_LINE_SIZE * 2);
        bin->max_spans = available_bytes / exact_size;
        
        if (bin->max_spans > 64) bin->max_spans = 64;
        bin->used_spans = 0;
        
        /* Initialize the usage bitmap (1 = Busy, 0 = Free) */
        if (bin->max_spans == 64) {
            bin->usage_bitmap = 0ULL;
        } else {
            /* Set bits beyond max_spans to 1 to mark them as occupied */
            bin->usage_bitmap = ~((1ULL << bin->max_spans) - 1);
        }

        /* O(1) Head Insertion into the core's active span list */
        bin->next = heap->active_spans;
        bin->prev = NULL;
        if (heap->active_spans) {
            heap->active_spans->prev = bin;
        }
        heap->active_spans = bin;

        LZ_DEBUG("Span: Provisioned Chunk for size %zu", exact_size);
    }

    /* 3. Fast-Path: Bit-Leaping O(1) using Hardware Intrinsics */
    uint64_t free_mask = ~bin->usage_bitmap;
    uint32_t free_idx = (uint32_t)__builtin_ctzll(free_mask);
    
    bin->usage_bitmap |= (1ULL << free_idx);
    bin->used_spans++;

    /* --- ACTIVE UNLINKING: Prevent O(N) traversal of full spans --- */
    if (LZ_UNLIKELY(bin->used_spans == bin->max_spans)) {
        if (bin->prev) {
            bin->prev->next = bin->next;
        } else {
            heap->active_spans = bin->next;
        }
        if (bin->next) {
            bin->next->prev = bin->prev;
        }
        bin->next = NULL;
        bin->prev = NULL;
    }

    /* Calculate the effective payload address using index-based arithmetic */
    uintptr_t chunk_base = (uintptr_t)bin & LZ_CHUNK_MASK;
    uintptr_t payload_base = chunk_base + (LZ_CACHE_LINE_SIZE * 2);
    
    return (void*)(payload_base + (free_idx * bin->span_size));
}

/**
 * @brief Deallocates a medium object back to its local span bin.
 * @details Uses address-to-offset arithmetic to determine the bit index 
 * in the usage bitmap.
 * @param heap Current core-local heap.
 * @param chunk Metadata header of the target span chunk.
 * @param ptr Virtual address of the span to release.
 */
void lz_span_free_local(lz_core_heap_t* heap, lz_chunk_t* chunk, void* ptr) {
    lz_span_bin_t* bin = (lz_span_bin_t*)((uint8_t*)chunk + LZ_CACHE_LINE_SIZE);
    
    uintptr_t chunk_base = (uintptr_t)chunk;
    uintptr_t payload_base = chunk_base + (LZ_CACHE_LINE_SIZE * 2);
    uintptr_t offset = (uintptr_t)ptr - payload_base;
    
    /* Inverse arithmetic to find the bitmap index */
    uint32_t idx = (uint32_t)(offset / bin->span_size);

    bool was_full = (bin->used_spans == bin->max_spans);

    /* Flip the bit back to zero (free) */
    bin->usage_bitmap &= ~(1ULL << idx);
    bin->used_spans--;

    /* --- ACTIVE RELINKING --- */
    if (LZ_UNLIKELY(was_full)) {
        bin->next = heap->active_spans;
        bin->prev = NULL;
        if (heap->active_spans) {
            heap->active_spans->prev = bin;
        }
        heap->active_spans = bin;
        return; 
    }

    /* --- THERMAL HYSTERESIS & RECLAMATION --- */
    if (LZ_UNLIKELY(bin->used_spans == 0)) {
        /* Anti-chattering: Keep at least one empty span in the list */
        if (heap->active_spans == bin && bin->next == NULL) {
            return;
        }

        if (bin->prev) {
            bin->prev->next = bin->next;
        } else {
            heap->active_spans = bin->next;
        }
        
        if (bin->next) {
            bin->next->prev = bin->prev;
        }

        lz_pagemap_set_slow(chunk, NULL);
        lz_vmm_free_chunk(chunk);
        LZ_DEBUG("Span: Chunk returned to VMM");
    }
}