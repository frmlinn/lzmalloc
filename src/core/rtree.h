/**
 * @file rtree.h
 * @brief Lock-free 2-level Radix Tree for O(1) Chunk metadata resolution.
 */

#ifndef LZ_RTREE_H
#define LZ_RTREE_H

#include "common.h"
#include "chunk.h"

/* ========================================================================= *
 * Bit Configuration (Targeting 48-bit to 52-bit Virtual Addresses)
 * ========================================================================= */

/** @def LZ_RTREE_L2_BITS 
 * @brief Bits allocated for Level 2 (Leaves): 14 bits yielding 16384 entries. */
#define LZ_RTREE_L2_BITS 14
#define LZ_RTREE_L2_ENTRIES (1 << LZ_RTREE_L2_BITS)
#define LZ_RTREE_L2_MASK (LZ_RTREE_L2_ENTRIES - 1)

/** @def LZ_RTREE_L1_BITS 
 * @brief Bits allocated for Level 1 (Root): 13 bits yielding 8192 entries. */
#define LZ_RTREE_L1_BITS 13
#define LZ_RTREE_L1_ENTRIES (1 << LZ_RTREE_L1_BITS)
#define LZ_RTREE_L1_MASK (LZ_RTREE_L1_ENTRIES - 1)

/* ========================================================================= *
 * Tree Structures
 * ========================================================================= */

/**
 * @struct lz_rtree_leaf_t
 * @brief Level 2 Leaf Node. Holds direct atomic pointers to Chunk headers.
 */
typedef struct {
    _Atomic(lz_chunk_header_t*) entries[LZ_RTREE_L2_ENTRIES];
} lz_rtree_leaf_t;

/**
 * @struct lz_rtree_root_t
 * @brief Level 1 Root Node. Holds atomic pointers to L2 leaves.
 */
typedef struct {
    _Atomic(lz_rtree_leaf_t*) leaves[LZ_RTREE_L1_ENTRIES];
} lz_rtree_root_t;

/* Global root exported for inline fast-path resolution */
extern lz_rtree_root_t g_rtree_root;

/* ========================================================================= *
 * Radix Tree Public API
 * ========================================================================= */

/**
 * @brief Bootstraps the global Radix Tree root node.
 */
void lz_rtree_init(void);

/**
 * @brief Thread-safe, lock-free registration of a Chunk into the global tree.
 * @param chunk_addr The base virtual address of the Chunk.
 * @param metadata Pointer to the corresponding chunk header.
 */
void lz_rtree_set(uintptr_t chunk_addr, lz_chunk_header_t* metadata);

/**
 * @brief Resolves the owning Chunk metadata for any arbitrary pointer in O(1).
 * @note Critical hot-path function. Forced inline.
 * * @param ptr Any address within the allocator's space (e.g., from user's free()).
 * @return Pointer to the Chunk header, or NULL if unmapped.
 */
static LZ_ALWAYS_INLINE lz_chunk_header_t* lz_rtree_get(const void* ptr) {
    uintptr_t block_num = (uintptr_t)ptr >> LZ_CHUNK_SHIFT;
    uintptr_t l2_idx = block_num & LZ_RTREE_L2_MASK;
    uintptr_t l1_idx = (block_num >> LZ_RTREE_L2_BITS) & LZ_RTREE_L1_MASK;

    /* Level 1: Acquire semantics to synchronize leaf node creation */
    lz_rtree_leaf_t* leaf = atomic_load_explicit(&g_rtree_root.leaves[l1_idx], memory_order_acquire);
    if (LZ_UNLIKELY(!leaf)) {
        return NULL; 
    }

    /* Level 2: Relaxed load is sufficient due to previous Acquire barrier */
    return atomic_load_explicit(&leaf->entries[l2_idx], memory_order_relaxed);
}

/**
 * @brief Safely removes a mapping from the tree.
 * @param chunk_addr The base virtual address of the Chunk.
 */
void lz_rtree_clear(uintptr_t chunk_addr);

#endif /* LZ_RTREE_H */