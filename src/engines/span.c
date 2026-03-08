/**
 * @file span.c
 * @brief Implementation of the contiguous page allocator for medium objects.
 */

#include "span.h"
#include "vmm.h"
#include "rtree.h"
#include <string.h>

/* ========================================================================= *
 * Internal Helpers
 * ========================================================================= */

static int find_contiguous_pages(lz_span_t* span, uint32_t required_pages) {
    uint32_t consecutive = 0;
    int start_idx = -1;

    for (uint32_t i = 0; i < LZ_SPAN_BITMAP_WORDS; i++) {
        uint64_t word = span->free_bitmap[i];
        
        if (word == 0) {
            consecutive = 0;
            continue;
        }

        for (uint32_t bit = 0; bit < 64; bit++) {
            if (word & (1ULL << bit)) {
                if (consecutive == 0) {
                    start_idx = (i * 64) + bit;
                }
                consecutive++;

                if (consecutive == required_pages) {
                    /* Match found: Mark bits as used (0) */
                    for (uint32_t j = start_idx; j < start_idx + required_pages; j++) {
                        span->free_bitmap[j / 64] &= ~(1ULL << (j % 64));
                    }
                    return start_idx;
                }
            } else {
                consecutive = 0; 
            }
        }
    }
    return -1; 
}

/* ========================================================================= *
 * Main Allocation Engine
 * ========================================================================= */

void* lz_span_alloc(lz_tlh_t* tlh, size_t size) {
    uint32_t required_pages = (uint32_t)(LZ_ALIGN_UP(size, LZ_PAGE_SIZE) / LZ_PAGE_SIZE);
    
    /* CRITICAL GUARD: Prevent Buffer Overflow on metadata array */
    if (LZ_UNLIKELY(required_pages >= LZ_SPAN_TOTAL_PAGES)) {
        return NULL; 
    }

    lz_span_t* current = tlh->active_spans;
    int page_idx = -1;

    /* 1. FAST PATH: Search existing active spans */
    while (current != NULL) {
        if (LZ_SPAN_TOTAL_PAGES - current->used_pages >= required_pages) {
            page_idx = find_contiguous_pages(current, required_pages);
            if (page_idx != -1) {
                break;
            }
        }
        current = current->next;
    }

    /* 2. SLOW PATH: Provision a new 2MB Chunk from the VMM */
    if (LZ_UNLIKELY(page_idx == -1)) {
        lz_chunk_header_t* chunk = lz_vmm_alloc_chunk();
        if (LZ_UNLIKELY(!chunk)) return NULL; 

        chunk->owning_tlh = tlh;
        chunk->chunk_type = LZ_CHUNK_TYPE_SPAN; /* Fixed Magic Number */
        chunk->checksum = lz_calc_checksum(chunk);
        
        lz_rtree_set((uintptr_t)chunk, chunk);
        
        current = (lz_span_t*)((char*)chunk + LZ_CACHE_LINE_SIZE);
        
        current->free_bitmap[0] = ~1ULL; 
        for (int i = 1; i < LZ_SPAN_BITMAP_WORDS; i++) {
            current->free_bitmap[i] = ~0ULL; 
        }
        
        current->used_pages = 1; 
        memset(current->alloc_size_pages, 0, sizeof(current->alloc_size_pages));

        current->prev = NULL;
        current->next = tlh->active_spans;
        if (tlh->active_spans) {
            tlh->active_spans->prev = current;
        }
        tlh->active_spans = current;

        page_idx = find_contiguous_pages(current, required_pages);
        
        /* Failsafe check to assert algorithm logic */
        if (LZ_UNLIKELY(page_idx == -1)) {
            LZ_FATAL("Span engine failed to map contiguous pages in pristine chunk.");
            return NULL;
        }
    }

    /* 3. Finalize Allocation */
    current->used_pages += required_pages;
    current->alloc_size_pages[page_idx] = (uint16_t)required_pages;

    uintptr_t chunk_base = (uintptr_t)current & ~(((uintptr_t)1 << LZ_CHUNK_SHIFT) - 1);
    return (void*)(chunk_base + (page_idx * LZ_PAGE_SIZE));
}

/* ========================================================================= *
 * Memory Release Engine
 * ========================================================================= */

void lz_span_free_local(lz_tlh_t* tlh, lz_chunk_header_t* chunk, void* ptr) {
    lz_span_t* span = (lz_span_t*)((char*)chunk + LZ_CACHE_LINE_SIZE);
    
    uintptr_t offset = (uintptr_t)ptr - (uintptr_t)chunk;
    uint32_t start_page = (uint32_t)(offset / LZ_PAGE_SIZE);
    
    if (LZ_UNLIKELY(start_page == 0 || start_page >= LZ_SPAN_TOTAL_PAGES)) {
        return; 
    }

    uint32_t required_pages = span->alloc_size_pages[start_page];
    if (LZ_UNLIKELY(required_pages == 0)) {
        return; /* Double-free protection */
    }

    /* Mark the pages as free (1) in the bitmap */
    for (uint32_t j = start_page; j < start_page + required_pages; j++) {
        span->free_bitmap[j / 64] |= (1ULL << (j % 64));
    }
    
    span->alloc_size_pages[start_page] = 0;
    span->used_pages -= required_pages;

    /* Telemetry Batching */
    if (LZ_UNLIKELY(tlh->stat_slot)) {
        tlh->local_bytes_free_batch += (required_pages * LZ_PAGE_SIZE);
        if (LZ_UNLIKELY(tlh->local_bytes_free_batch >= 4096)) {
            atomic_fetch_sub_explicit(&tlh->stat_slot->data.bytes_allocated, tlh->local_bytes_free_batch, memory_order_relaxed);
            tlh->local_bytes_free_batch = 0;
        }
    }

    /* Destruction: Reclaim memory when completely empty */
    if (LZ_UNLIKELY(span->used_pages == 1)) {
        if (span->prev) span->prev->next = span->next;
        else tlh->active_spans = span->next;
        
        if (span->next) span->next->prev = span->prev;

        lz_rtree_clear((uintptr_t)chunk);
        lz_vmm_free_chunk(chunk);
    }
}