/**
 * @file rtree.h
 * @brief Lock-free 2-level Radix Tree for lzmalloc V2.
 * Specialized in registering and resolving 2MB Superblocks (Chunks).
 */

#ifndef LZ_RTREE_H
#define LZ_RTREE_H

#include "common.h"
#include "chunk.h"

/* ========================================================================= *
 * Bit Configuration (Targeting 48-bit to 52-bit Virtual Addresses)
 * ========================================================================= */

// Note: LZ_CHUNK_SHIFT is now dynamically provided by lz_config.h
// 21 bits for x86_64 (2MB), 25 bits for Apple Silicon (32MB).

/** @brief Bits for Level 2 (Leaves): 14 bits = 16384 entries. */
#define LZ_RTREE_L2_BITS 14
#define LZ_RTREE_L2_ENTRIES (1 << LZ_RTREE_L2_BITS)
#define LZ_RTREE_L2_MASK (LZ_RTREE_L2_ENTRIES - 1)

/** @brief Bits for Level 1 (Root): 13 bits = 8192 entries. */
#define LZ_RTREE_L1_BITS 13
#define LZ_RTREE_L1_ENTRIES (1 << LZ_RTREE_L1_BITS)
#define LZ_RTREE_L1_MASK (LZ_RTREE_L1_ENTRIES - 1)

/* ========================================================================= *
 * Tree Structures
 * ========================================================================= */

/**
 * @brief Leaf Node (Level 2). Contains direct pointers to Chunk metadata.
 */
typedef struct {
    _Atomic(lz_chunk_header_t*) entries[LZ_RTREE_L2_ENTRIES];
} lz_rtree_leaf_t;

/**
 * @brief Root Node (Level 1). Contains atomic pointers to leaves.
 */
typedef struct {
    _Atomic(lz_rtree_leaf_t*) leaves[LZ_RTREE_L1_ENTRIES];
} lz_rtree_root_t;

/* ========================================================================= *
 * Radix Tree Public API
 * ========================================================================= */

/**
 * @brief Initializes the global tree structure (must be called at startup).
 */
void lz_rtree_init(void);

/**
 * @brief Registers a Chunk in the tree. Thread-safe and Lock-free.
 * @param chunk_addr Base address of the Chunk.
 * @param metadata Pointer to the header controlling this space.
 */
void lz_rtree_set(uintptr_t chunk_addr, lz_chunk_header_t* metadata);

/**
 * @brief Resolves which Chunk an arbitrary pointer belongs to. Lock-free.
 * @note Critical function. Must be inline for maximum fast-path performance.
 * @param ptr Arbitrary pointer (e.g., passed by the user to free()).
 * @return Pointer to the Chunk metadata, or NULL if unregistered.
 */
static LZ_ALWAYS_INLINE lz_chunk_header_t* lz_rtree_get(const void* ptr) {
    extern lz_rtree_root_t g_rtree_root;

    // Discard the offset within the chunk to get the block number
    uintptr_t block_num = (uintptr_t)ptr >> LZ_CHUNK_SHIFT;
    
    // Extract indices using bitwise masks
    uintptr_t l2_idx = block_num & LZ_RTREE_L2_MASK;
    uintptr_t l1_idx = (block_num >> LZ_RTREE_L2_BITS) & LZ_RTREE_L1_MASK;

    // Level 1: Read the corresponding leaf (Acquire semantics)
    lz_rtree_leaf_t* leaf = atomic_load_explicit(&g_rtree_root.leaves[l1_idx], memory_order_acquire);
    if (LZ_UNLIKELY(!leaf)) {
        return NULL; // Memory not registered in our system
    }

    // Level 2: Read the metadata (Relaxed is enough due to prior Acquire barrier)
    return atomic_load_explicit(&leaf->entries[l2_idx], memory_order_relaxed);
}

/**
 * @brief Removes a mapping from the tree.
 * @param chunk_addr Base address of the Chunk.
 */
void lz_rtree_clear(uintptr_t chunk_addr);

#endif // LZ_RTREE_H