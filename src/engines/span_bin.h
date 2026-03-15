/**
 * @file span_bin.h
 * @brief Segregated Span Engine for Medium Objects (32KB - 1MB).
 * @details Uses a bit-leaping strategy with 64-bit bitmaps to manage 
 * page-aligned partitions within a chunk.
 */
#ifndef LZ_ENGINE_SPAN_BIN_H
#define LZ_ENGINE_SPAN_BIN_H

#include "chunk.h"
#include "core_heap.h"
#include <stddef.h>

/**
 * @struct lz_span_bin_s
 * @brief Span Metadata Header.
 * @details Resides on the second cache line of the Chunk (Offset 0x40).
 */
typedef struct LZ_CACHELINE_ALIGNED lz_span_bin_s {
    struct lz_span_bin_s* next;
    struct lz_span_bin_s* prev;

    uint32_t span_size;     /**< Size of each segment in this span chunk. */
    uint32_t max_spans;     /**< Total segments available (max 64). */
    uint32_t used_spans;    /**< Count of occupied segments. */
    
    /** @brief 64-bit usage bitmap. 1 = Occupied, 0 = Free. */
    uint64_t usage_bitmap;
    
} lz_span_bin_t;

/**
 * @brief Allocates a medium object from the local span pool.
 */
void* lz_span_alloc_local(lz_core_heap_t* heap, uint32_t size_class_idx, size_t exact_size);

/**
 * @brief Deallocates a medium object back to its originating span.
 */
void lz_span_free_local(lz_core_heap_t* heap, lz_chunk_t* chunk, void* ptr);

#endif /* LZ_ENGINE_SPAN_BIN_H */