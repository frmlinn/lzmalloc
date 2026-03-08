/**
 * @file common.h
 * @brief Hardware primitives, bitwise math, and compiler abstractions for lzmalloc V2.
 * * @note This file represents Layer 0. It strictly defines macros and built-ins.
 * It must not contain state, business logic, or external dependencies.
 */

#ifndef LZ_COMMON_H
#define LZ_COMMON_H

/* ========================================================================= *
 * Standard System Dependencies (C11)
 * ========================================================================= */
#include <stddef.h>    
#include <stdint.h>    
#include <stdbool.h>   
#include <assert.h>    
#include <stdatomic.h> 
#include "lz_log.h"    

/* ========================================================================= *
 * Architecture Constants
 * ========================================================================= */
#include "lz_config.h"

/* ========================================================================= *
 * Compiler Attributes and Directives (GCC/Clang)
 * ========================================================================= */

/**
 * @def LZ_LIKELY(x)
 * @brief Static branch prediction (Likely).
 * Optimizes the CPU instruction pipeline by placing the block in the sequential flow.
 */
#define LZ_LIKELY(x)   __builtin_expect(!!(x), 1)

/**
 * @def LZ_UNLIKELY(x)
 * @brief Static branch prediction (Unlikely).
 * Moves error-handling or edge-case code away from the hot-path assembly.
 */
#define LZ_UNLIKELY(x) __builtin_expect(!!(x), 0)

/**
 * @def LZ_CACHE_ALIGNED
 * @brief Forces structural alignment to the L1/L2 cache line boundaries.
 * Critical for preventing False Sharing in multi-core NUMA environments.
 */
#define LZ_CACHE_ALIGNED __attribute__((aligned(LZ_CACHE_LINE_SIZE)))

/**
 * @def LZ_ALWAYS_INLINE
 * @brief Forces the compiler to inline the function, bypassing heuristic limits.
 */
#define LZ_ALWAYS_INLINE inline __attribute__((always_inline))

/* ========================================================================= *
 * Pure Mathematics (Bitwise Operations)
 * ========================================================================= */

/**
 * @def LZ_IS_POWER_OF_TWO(x)
 * @brief Validates if a strictly positive integer is a power of 2.
 */
#define LZ_IS_POWER_OF_TWO(x) (((x) != 0) && (((x) & ((x) - 1)) == 0))

/**
 * @def LZ_ALIGN_UP(size, align)
 * @brief Aligns a size UP to the nearest multiple of 'align' (must be power of 2).
 */
#define LZ_ALIGN_UP(size, align) (((size) + ((align) - 1)) & ~((align) - 1))

/**
 * @def LZ_ALIGN_DOWN(size, align)
 * @brief Aligns a size DOWN to the nearest multiple of 'align' (must be power of 2).
 */
#define LZ_ALIGN_DOWN(size, align) ((size) & ~((align) - 1))

/* ========================================================================= *
 * Hardware Primitives
 * ========================================================================= */

/**
 * @brief Emits a CPU pause instruction for active wait loops (Spinlocks).
 * Prevents excessive power consumption and CPU pipeline contention during atomic waits.
 */
static LZ_ALWAYS_INLINE void lz_cpu_relax(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause" ::: "memory");
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield" ::: "memory");
#else
    __asm__ volatile("" ::: "memory"); // Full compiler barrier fallback
#endif
}

#endif /* LZ_COMMON_H */