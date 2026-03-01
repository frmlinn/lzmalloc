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

/** @brief Minimum allocation size. 16 bytes allows storing lock-free pointers (next). */
#define LZ_MIN_ALLOC_SIZE 16

/** @brief Threshold where we switch from purely linear bins to logarithmic scaling. */
#define LZ_SMALL_LINEAR_MAX 128

/** * @brief Total amount of Size Classes supported by the Thread-Local Heap.
 * 88 classes support objects up to ~112MB. 
 * WARNING: If LZ_MAX_SLAB_OBJ_SIZE is configured in CMake above 112MB,
 * this value MUST be increased to prevent a Buffer Overflow in tlh->bins[].
 */
#define LZ_MAX_SIZE_CLASSES 88

/* ========================================================================= *
 * Intrinsic Mathematical Helpers
 * ========================================================================= */

/**
 * @brief Gets the position of the Most Significant Bit (MSB) in O(1) using hardware.
 * @param x Value to evaluate.
 * @return The 0-based index of the MSB.
 */
static LZ_ALWAYS_INLINE uint32_t lz_fls(size_t x) {
    // __builtin_clzll counts leading zeros.
    // 63 - clz gives us the MSB index.
    return x == 0 ? 0 : 63 - __builtin_clzll(x);
}

/* ========================================================================= *
 * Main Size Resolution API
 * ========================================================================= */

/**
 * @brief Converts a requested size in bytes to its Size Class index.
 * @param size The size requested by lz_malloc().
 * @return The index (0 to LZ_MAX_SIZE_CLASSES - 1).
 */
static LZ_ALWAYS_INLINE uint32_t lz_size_to_class(size_t size) {
    if (LZ_UNLIKELY(size <= LZ_MIN_ALLOC_SIZE)) {
        return 0;
    }

    size_t s = size - 1; // Adjustment so exact multiples don't jump to the next class

    // FAST PATH 1: Small sizes (< 128 bytes). Linear increments of 16 bytes.
    // Classes: 0(16), 1(32), 2(48), 3(64), 4(80), 5(96), 6(112), 7(128)
    if (LZ_LIKELY(s < LZ_SMALL_LINEAR_MAX)) {
        return (uint32_t)(s >> 4);
    }

    // FAST PATH 2: Logarithmic-Linear scaling (>= 128 bytes).
    // Guarantees < 25% internal fragmentation. 4 subdivisions per power of 2.
    uint32_t msb = lz_fls(s);
    uint32_t shift = msb - 2;
    uint32_t base = (msb - 6) << 2;       // Calculates the group start (power of 2)
    uint32_t offset = (s >> shift) & 3;   // Extracts the sub-bin (0, 1, 2 or 3)

    return 8 + base + offset;
}

/**
 * @brief Returns the actual size to be allocated for a given Size Class.
 * (The exact mathematical inverse of lz_size_to_class).
 * @param class_idx The target Size Class index.
 * @return The aligned size in bytes.
 */
static LZ_ALWAYS_INLINE size_t lz_class_to_size(uint32_t class_idx) {
    if (class_idx < 8) {
        return (class_idx + 1) << 4; // (idx + 1) * 16
    }
    
    uint32_t base_idx = class_idx - 8;
    uint32_t group = base_idx >> 2;
    uint32_t shift = group + 5;
    uint32_t offset = base_idx & 3;
    
    return (size_t)(4 + offset) << shift;
}

#endif // LZ_SIZES_H