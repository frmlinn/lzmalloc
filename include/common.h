/**
 * @file common.h
 * @brief Hardware primitives, bitwise math, and compiler abstractions for lzmalloc V2.
 * @note This file represents Layer 0. It must not contain state or business logic.
 */

#ifndef LZ_COMMON_H
#define LZ_COMMON_H

/* ========================================================================= *
 * Standard System Dependencies (C11)
 * ========================================================================= */
#include <stddef.h>    // size_t, ptrdiff_t
#include <stdint.h>    // Fixed-width integer types
#include <stdbool.h>   // bool, true, false
#include <assert.h>    // assert() for Debug mode validations
#include <stdatomic.h> // Native C11 atomics (memory_order_*)
#include "lz_log.h"    // Logging helpers

/* ========================================================================= *
 * Architecture Constants (Generated dynamically)
 * ========================================================================= */
#include "lz_config.h"

/* ========================================================================= *
 * Compiler Attributes and Directives (GCC/Clang)
 * ========================================================================= */

/**
 * @brief Static branch prediction (Likely).
 * Informs the compiler that expression 'x' is VERY LIKELY to be true.
 * Optimizes the CPU instruction pipeline by placing the 'if' block in the sequential flow.
 */
#define LZ_LIKELY(x)   __builtin_expect(!!(x), 1)

/**
 * @brief Static branch prediction (Unlikely).
 * Informs the compiler that expression 'x' is VERY UNLIKELY to be true.
 * Moves error-handling code away from the "hot-path" in the generated assembly.
 */
#define LZ_UNLIKELY(x) __builtin_expect(!!(x), 0)

/**
 * @brief Data structure alignment.
 * Forces the compiler to align a variable or struct to the cache line boundaries.
 * CRITICAL for preventing "False Sharing" in multi-core environments.
 */
#define LZ_CACHE_ALIGNED __attribute__((aligned(LZ_CACHE_LINE_SIZE)))

/**
 * @brief Forces the compiler to always inline the function.
 */
#define LZ_ALWAYS_INLINE inline __attribute__((always_inline))

/* ========================================================================= *
 * Pure Mathematics (Bitwise Operations)
 * ========================================================================= */

/**
 * @brief Checks if a given value is a power of 2.
 * @param x Value to evaluate (must be greater than 0).
 * @return true if it is a power of 2, false otherwise.
 */
#define LZ_IS_POWER_OF_TWO(x) (((x) != 0) && (((x) & ((x) - 1)) == 0))

/**
 * @brief Aligns a size UP to the nearest multiple of 'align'.
 * @note 'align' MUST be a power of 2.
 * @param size The original size.
 * @param align The desired alignment boundary (e.g., 8, 16, 4096).
 * @return The upward-aligned size.
 */
#define LZ_ALIGN_UP(size, align) (((size) + ((align) - 1)) & ~((align) - 1))

/**
 * @brief Aligns a size DOWN to the nearest multiple of 'align'.
 * @note 'align' MUST be a power of 2.
 * @param size The original size.
 * @param align The desired alignment boundary.
 * @return The downward-aligned size.
 */
#define LZ_ALIGN_DOWN(size, align) ((size) & ~((align) - 1))

/* ========================================================================= *
 * Hardware Primitives
 * ========================================================================= */

/**
 * @brief CPU pause for active wait loops (Spinlocks).
 * Prevents excessive power consumption and CPU pipeline contention
 * when a thread is waiting for an atomic variable to change state.
 */
static LZ_ALWAYS_INLINE void lz_cpu_relax(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause" ::: "memory");
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield" ::: "memory");
#else
    // Generic fallback: compiler memory barrier
    __asm__ volatile("" ::: "memory");
#endif
}

#endif // LZ_COMMON_H