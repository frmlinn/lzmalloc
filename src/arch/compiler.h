/**
 * @file compiler.h
 * @brief Compiler-specific Optimization Directives.
 * @details Defines macros for Branch Target Prediction, I-Cache optimization, 
 * and memory layout alignment to ensure mechanical sympathy.
 */
#ifndef LZ_COMPILER_H
#define LZ_COMPILER_H

#include "lz_config.h"

/* -------------------------------------------------------------------------- *
 * Branch Prediction (BTB Optimization)
 * -------------------------------------------------------------------------- */
/** @brief Static hint: Condition is expected to be True. */
#define LZ_LIKELY(x)   __builtin_expect(!!(x), 1)
/** @brief Static hint: Condition is expected to be False. */
#define LZ_UNLIKELY(x) __builtin_expect(!!(x), 0)

/* -------------------------------------------------------------------------- *
 * Inlining & Code Locality
 * -------------------------------------------------------------------------- */
/** @brief Force-inline a function to eliminate call overhead in hot paths. */
#define LZ_ALWAYS_INLINE inline __attribute__((always_inline))

/** @brief Mark a function as cold, moving it to the .text.unlikely section. */
#define LZ_COLD_PATH __attribute__((cold, noinline))

/* -------------------------------------------------------------------------- *
 * Memory Layout & Alignment
 * -------------------------------------------------------------------------- */
/** @brief Align a structure to a cache line boundary to prevent False Sharing. */
#define LZ_CACHELINE_ALIGNED __attribute__((aligned(LZ_CACHE_LINE_SIZE)))

/** @brief Disable automatic padding in structures for dense metadata packing. */
#define LZ_PACKED __attribute__((packed))

/* -------------------------------------------------------------------------- *
 * Strict Aliasing
 * -------------------------------------------------------------------------- */
/** @brief Indicates that a pointer does not alias with any other pointer. */
#define LZ_RESTRICT __restrict__

#endif /* LZ_COMPILER_H */