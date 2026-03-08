/**
 * @file sizes.h
 * @brief Size Classes mathematics and fragmentation calculation for lzmalloc V2.
 */

#ifndef LZ_SIZES_H
#define LZ_SIZES_H

#include "common.h"

/* ========================================================================= *
 * Size Classes Configuration
 * ========================================================================= */

/** @def LZ_MIN_ALLOC_SIZE 
 * @brief Minimum allocation size (16 bytes) to safely store lock-free next pointers. */
#define LZ_MIN_ALLOC_SIZE 16

/** @def LZ_SMALL_LINEAR_MAX 
 * @brief Threshold byte size to transition from linear bins to logarithmic scaling. */
#define LZ_SMALL_LINEAR_MAX 128

/** @def LZ_MAX_SIZE_CLASSES 
 * @brief Maximum number of Size Classes supported by the Thread-Local Heap (TLH). */
#define LZ_MAX_SIZE_CLASSES 88

/* ========================================================================= *
 * Intrinsic Mathematical Helpers
 * ========================================================================= */

/**
 * @brief Computes the 0-based index of the Most Significant Bit (MSB) in O(1).
 * * @param x The size value to evaluate.
 * @return The index of the MSB.
 */
static LZ_ALWAYS_INLINE uint32_t lz_fls(size_t x) {
    return x == 0 ? 0 : 63 - __builtin_clzll((unsigned long long)x);
}

/* ========================================================================= *
 * Main Size Resolution API
 * ========================================================================= */

/**
 * @brief Translates a requested byte size into its corresponding Size Class index.
 * * @param size The raw memory size requested by the application.
 * @return The target Size Class index (0 to LZ_MAX_SIZE_CLASSES - 1).
 */
static LZ_ALWAYS_INLINE uint32_t lz_size_to_class(size_t size) {
    if (LZ_UNLIKELY(size <= LZ_MIN_ALLOC_SIZE)) {
        return 0;
    }

    size_t s = size - 1; 

    if (LZ_LIKELY(s < LZ_SMALL_LINEAR_MAX)) {
        return (uint32_t)(s >> 4);
    }

    uint32_t msb = lz_fls(s);
    uint32_t shift = msb - 2;
    uint32_t base = (msb - 6) << 2;       
    uint32_t offset = (s >> shift) & 3;   

    return 8 + base + offset;
}

/**
 * @brief Calculates the exact aligned byte size for a given Size Class index.
 * * @param class_idx The target Size Class index.
 * @return The mathematically aligned size in bytes.
 */
static LZ_ALWAYS_INLINE size_t lz_class_to_size(uint32_t class_idx) {
    if (LZ_LIKELY(class_idx < 8)) {
        return (class_idx + 1) << 4; 
    }
    
    uint32_t base_idx = class_idx - 8;
    uint32_t group = base_idx >> 2;
    uint32_t shift = group + 5;
    uint32_t offset = base_idx & 3;
    
    return (size_t)(4 + offset) << shift;
}

#endif /* LZ_SIZES_H */