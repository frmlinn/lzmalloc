/**
 * @file span.h
 * @brief Binned Bitmap Span engine for Medium Objects (32KB - 1MB).
 * @details Manages 2MB Superblocks partitioned into OS-sized pages (4KB).
 * Eliminates internal fragmentation for objects too large for Slabs but too 
 * small for direct VMM allocations.
 */

#ifndef LZ_SPAN_H
#define LZ_SPAN_H

#include "common.h"
#include "chunk.h"
#include "tlh.h"

/* ========================================================================= *
 * Span Configuration
 * ========================================================================= */

/** @brief A 2MB Chunk holds exactly 512 pages of 4KB. */
#define LZ_SPAN_TOTAL_PAGES 512

/** @brief 512 bits required for the bitmap = 8 x 64-bit words. */
#define LZ_SPAN_BITMAP_WORDS (LZ_SPAN_TOTAL_PAGES / 64)

/* ========================================================================= *
 * Span Metadata Structure
 * ========================================================================= */

/**
 * @struct lz_span_t
 * @brief Tracks contiguous page allocations within a 2MB Chunk.
 * @note Resides in Page 0 immediately after the 64-byte lz_chunk_header_t.
 */
typedef struct lz_span_s {
    struct lz_span_s* next;     /**< Doubly-linked list pointer (active_spans) */
    struct lz_span_s* prev;     /**< Doubly-linked list pointer (active_spans) */
    
    uint64_t free_bitmap[LZ_SPAN_BITMAP_WORDS]; /**< 1 = Free, 0 = Used */
    uint32_t used_pages;                        /**< Count of consumed pages */
    uint32_t _padding;                          /**< Alignment padding */

    /** * @brief O(1) size lookup for free(). 
     * Stores the length (in pages) of the allocation starting at index 'i'.
     * Occupies 1024 bytes, fitting perfectly within the reserved Page 0.
     */
    uint16_t alloc_size_pages[LZ_SPAN_TOTAL_PAGES]; 
} lz_span_t;

/* ========================================================================= *
 * Public API
 * ========================================================================= */

/**
 * @brief Allocates a medium object by reserving contiguous 4KB pages.
 * @param tlh The Thread-Local Heap of the executing thread.
 * @param size The requested size in bytes (32KB to 1MB).
 * @return Pointer to the allocated memory, or NULL if OOM.
 */
void* lz_span_alloc(lz_tlh_t* tlh, size_t size);

/**
 * @brief Frees a medium object from the local thread context.
 * @param tlh The Thread-Local Heap of the executing thread.
 * @param chunk Pointer to the Chunk header containing the Span.
 * @param ptr Pointer to the user payload to free.
 */
void lz_span_free_local(lz_tlh_t* tlh, lz_chunk_header_t* chunk, void* ptr);

/**
 * @brief Frees a medium object received via the atomic remote mailbox.
 * @note Semantically identical to local free since it is processed in the reap phase.
 */
static LZ_ALWAYS_INLINE void lz_span_free_remote(lz_tlh_t* tlh, lz_chunk_header_t* chunk, void* ptr) {
    lz_span_free_local(tlh, chunk, ptr);
}

#endif /* LZ_SPAN_H */