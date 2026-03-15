/**
 * @file pagemap.h
 * @brief O(1) Metadata Resolution Interface.
 * @details Implements a Flat Virtual Array optimized for 5-Level Paging systems.
 * This approach provides deterministic metadata resolution from any arbitrary 
 * pointer by shifting the address to calculate a direct index.
 */
#ifndef LZ_PAGEMAP_H
#define LZ_PAGEMAP_H

#include <stdint.h>
#include <stddef.h>
#include "compiler.h"
#include "lz_config.h"
#include "chunk.h"
#include "atomics.h"

/**
 * @brief Pagemap entries for 57-bit Virtual Addressing.
 * Formula: 2^(VA_BITS - CHUNK_SHIFT). For 57-bit VA and 2MB Chunks (2^21),
 * we require 2^36 entries.
 */
#define PAGEMAP_ENTRIES (1ULL << (57 - LZ_CHUNK_SHIFT))

/** @brief Global atomic pointer to the pagemap array. Exported for extreme inlining. */
extern _Atomic(lz_chunk_t**) g_pagemap;

/**
 * @brief Initializes the Flat Virtual Array.
 * @note Idempotent. Uses lock-free CAS for safe initialization during bootstrap.
 */
void lz_pagemap_init(void);

/**
 * @brief Sets a metadata entry for a specific 2MB region.
 * @param ptr Pointer within the target 2MB region.
 * @param meta Pointer to the chunk header to associate.
 */
void lz_pagemap_set_slow(void* ptr, lz_chunk_t* meta);

/**
 * @brief Retrieves metadata for a specific pointer (Cold-path).
 * @param ptr Pointer to resolve.
 * @return Associated lz_chunk_t pointer or NULL.
 */
lz_chunk_t* lz_pagemap_get_slow(void* ptr);

/**
 * @brief Ultra-Fast Metadata Resolution (Hot-path).
 * @details Performs a bit-shift on the provided pointer to index into the 
 * global pagemap. Designed to be fully thread-safe and signal-safe.
 * @param ptr Arbitrary pointer to resolve.
 * @return The owning lz_chunk_t header, or NULL for foreign pointers (e.g., glibc).
 * @note Uses Acquire semantics to ensure memory visibility of the pagemap.
 */
static LZ_ALWAYS_INLINE lz_chunk_t* lz_meta_resolve(void* ptr) {
    lz_chunk_t** map = lz_atomic_load_acquire(&g_pagemap);
    if (LZ_UNLIKELY(!map)) return NULL;
    
    /* Calculate index by shifting the pointer by 21 bits (2MB granularity) */
    uintptr_t idx = ((uintptr_t)ptr >> LZ_CHUNK_SHIFT) & (PAGEMAP_ENTRIES - 1);
    return map[idx];
}

#endif /* LZ_PAGEMAP_H */