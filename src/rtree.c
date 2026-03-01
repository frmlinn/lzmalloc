/**
 * @file rtree.c
 * @brief Implementation of the Lock-free Radix Tree.
 */

#include "rtree.h"
#include <sys/mman.h>
#include <stddef.h>

/* ========================================================================= *
 * Global State
 * ========================================================================= */

// The root of the tree is statically reserved in the BSS section.
// Occupies only 64KB (8192 * 8 bytes), an insignificant cost on modern systems.
lz_rtree_root_t g_rtree_root;

/* ========================================================================= *
 * Helper: Internal Node Allocation
 * ========================================================================= */

/**
 * @brief Requests memory directly from the OS for internal tree nodes.
 * We bypass lz_malloc to prevent infinite recursion during bootstrap.
 */
static void* alloc_internal_node(size_t size) {
    void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, 
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (ptr == MAP_FAILED) ? NULL : ptr;
}

/* ========================================================================= *
 * API Implementation
 * ========================================================================= */

void lz_rtree_init(void) {
    // Explicitly empty the pointers for C11 standard correctness.
    for (int i = 0; i < LZ_RTREE_L1_ENTRIES; ++i) {
        atomic_init(&g_rtree_root.leaves[i], NULL);
    }
}

void lz_rtree_set(uintptr_t chunk_addr, lz_chunk_header_t* metadata) {
    uintptr_t block_num = chunk_addr >> LZ_CHUNK_SHIFT;
    uintptr_t l2_idx = block_num & LZ_RTREE_L2_MASK;
    uintptr_t l1_idx = (block_num >> LZ_RTREE_L2_BITS) & LZ_RTREE_L1_MASK;

    // 1. Get the leaf or create it if it doesn't exist
    lz_rtree_leaf_t* leaf = atomic_load_explicit(&g_rtree_root.leaves[l1_idx], memory_order_acquire);
    
    if (LZ_UNLIKELY(!leaf)) {
        // Optimistic allocation of the leaf node
        lz_rtree_leaf_t* new_leaf = (lz_rtree_leaf_t*)alloc_internal_node(sizeof(lz_rtree_leaf_t));
        if (!new_leaf) return; // Catastrophic OS memory failure

        for (int i = 0; i < LZ_RTREE_L2_ENTRIES; ++i) {
            atomic_init(&new_leaf->entries[i], NULL);
        }

        // Compare-And-Swap to insert the leaf lock-free
        lz_rtree_leaf_t* expected = NULL;
        if (!atomic_compare_exchange_strong_explicit(&g_rtree_root.leaves[l1_idx], &expected, new_leaf, 
                                                     memory_order_release, memory_order_acquire)) {
            // We lost the race. Another thread created the leaf a nanosecond earlier.
            // Destroy ours and use the winner's leaf (now stored in 'expected').
            munmap(new_leaf, sizeof(lz_rtree_leaf_t));
            leaf = expected;
        } else {
            // We won the race.
            leaf = new_leaf;
        }
    }

    // 2. Insert the metadata into the leaf
    atomic_store_explicit(&leaf->entries[l2_idx], metadata, memory_order_release);
}

void lz_rtree_clear(uintptr_t chunk_addr) {
    lz_rtree_set(chunk_addr, NULL);
}