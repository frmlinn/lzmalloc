/**
 * @file routing.h
 * @brief Size Class Routing and Mathematical Fast-Paths.
 * @details Implements a logarithmic mapping from allocation sizes to 
 * normalized bins. Uses Bit Scan Reverse (BSR) hardware intrinsics to 
 * maintain O(1) routing complexity.
 */
#ifndef LZ_API_ROUTING_H
#define LZ_API_ROUTING_H

#include "compiler.h"
#include <stdint.h>
#include <stddef.h>

/** @brief Boundary for the Slab Engine (objects <= 32KB). */
#define LZ_MAX_SLAB_SIZE 32768
/** @brief Boundary for the Span Engine (objects <= 1MB). */
#define LZ_MAX_SPAN_SIZE 1048576

/**
 * @brief Maps an arbitrary byte size to a normalized Size Class.
 * @details Uses a 2nd-order logarithmic scale (8 bins per power of 2) 
 * to keep internal fragmentation below 12.5%.
 * @param size Requested size in bytes.
 * @param[out] out_idx The resulting size class index (0-87).
 * @param[out] out_block_size The exact block footprint.
 */
static LZ_ALWAYS_INLINE void lz_routing_get_size_class(size_t size, uint32_t* out_idx, uint32_t* out_block_size) {
    if (LZ_UNLIKELY(size == 0)) size = 8;
    
    /* Minimum 8-byte alignment for ABI compatibility */
    size = (size + 7) & ~7ULL; 

    /* Linear routing for the first 256 bytes */
    if (size <= 256) {
        *out_idx = (uint32_t)((size - 1) >> 3);
        *out_block_size = (uint32_t)size;
        return;
    }

    /**
     * @brief Logarithmic routing via Hardware BSR.
     * Maps size to a bin within the power-of-2 range.
     */
    uint32_t msb = 63 - (uint32_t)__builtin_clzll(size - 1);
    uint32_t shift = msb - 3;
    uint32_t base_size = 1U << msb;
    uint32_t offset = (uint32_t)(((size - 1) - base_size) >> shift);
    
    *out_block_size = base_size + ((offset + 1) << shift);
    *out_idx = (msb << 3) + offset - 32; 
}

#endif /* LZ_API_ROUTING_H */