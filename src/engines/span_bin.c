/**
 * @file span_bin.c
 * @brief Implementación completa del Segregated Span Engine (O(1) Bit-Leaping).
 */
#include "span_bin.h"
#include "vmm.h"
#include "pagemap.h"
#include "lz_log.h"
#include <stddef.h>
#include <stdint.h>

void* lz_span_alloc_local(lz_core_heap_t* heap, uint32_t size_class_idx, size_t exact_size) {
    (void)size_class_idx;
    lz_span_bin_t* bin = heap->active_spans;

    while (LZ_LIKELY(bin != NULL)) {
        if (bin->span_size == exact_size && bin->used_spans < bin->max_spans) {
            break;
        }
        bin = bin->next;
    }

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
        
        /* Mitigación Estándar de C: Evita el UB por desplazamiento de 64 bits */
        if (bin->max_spans == 64) {
            bin->usage_bitmap = 0ULL;
        } else {
            bin->usage_bitmap = ~((1ULL << bin->max_spans) - 1);
        }

        bin->next = heap->active_spans;
        bin->prev = NULL;
        if (heap->active_spans) {
            heap->active_spans->prev = bin;
        }
        heap->active_spans = bin;

        LZ_DEBUG("Span: Aprovisionado Chunk para tamaño %zu (Max spans: %u)", exact_size, bin->max_spans);
    }

    /* Fast-Path Extremo: Bit-Leaping */
    uint64_t free_mask = ~bin->usage_bitmap;
    uint32_t free_idx = (uint32_t)__builtin_ctzll(free_mask);
    
    bin->usage_bitmap |= (1ULL << free_idx);
    bin->used_spans++;

    uintptr_t chunk_base = (uintptr_t)bin & LZ_CHUNK_MASK;
    uintptr_t payload_base = chunk_base + (LZ_CACHE_LINE_SIZE * 2);
    
    return (void*)(payload_base + (free_idx * bin->span_size));
}

void lz_span_free_local(lz_core_heap_t* heap, lz_chunk_t* chunk, void* ptr) {
    lz_span_bin_t* bin = (lz_span_bin_t*)((uint8_t*)chunk + LZ_CACHE_LINE_SIZE);
    
    uintptr_t chunk_base = (uintptr_t)chunk;
    uintptr_t payload_base = chunk_base + (LZ_CACHE_LINE_SIZE * 2);
    uintptr_t offset = (uintptr_t)ptr - payload_base;
    
    uint32_t idx = (uint32_t)(offset / bin->span_size);

    bin->usage_bitmap &= ~(1ULL << idx);
    bin->used_spans--;

    if (LZ_UNLIKELY(bin->used_spans == 0)) {
        /* Histéresis añadida: Proteger contra Ping-Pong térmico */
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
        LZ_DEBUG("Span: Chunk devuelto al VMM");
    }
}