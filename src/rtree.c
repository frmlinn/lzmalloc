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

lz_rtree_root_t g_rtree_root;

/* ========================================================================= *
 * Internal Helpers
 * ========================================================================= */

/**
 * @brief Requests raw memory from the OS for internal tree structures.
 * Bypasses the allocator itself to prevent recursive deadlocks during bootstrap.
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
    for (int i = 0; i < LZ_RTREE_L1_ENTRIES; ++i) {
        atomic_init(&g_rtree_root.leaves[i], NULL);
    }
}

void lz_rtree_set(uintptr_t chunk_addr, lz_chunk_header_t* metadata) {
    uintptr_t block_num = chunk_addr >> LZ_CHUNK_SHIFT;
    uintptr_t l2_idx = block_num & LZ_RTREE_L2_MASK;
    uintptr_t l1_idx = (block_num >> LZ_RTREE_L2_BITS) & LZ_RTREE_L1_MASK;

    lz_rtree_leaf_t* leaf = atomic_load_explicit(&g_rtree_root.leaves[l1_idx], memory_order_acquire);
    
    if (LZ_UNLIKELY(!leaf)) {
        lz_rtree_leaf_t* new_leaf = (lz_rtree_leaf_t*)alloc_internal_node(sizeof(lz_rtree_leaf_t));
        if (!new_leaf) return; /* Unrecoverable OS memory exhaustion */

        for (int i = 0; i < LZ_RTREE_L2_ENTRIES; ++i) {
            atomic_init(&new_leaf->entries[i], NULL);
        }

        /* Lock-free Compare-And-Swap. Resolves initialization races between threads. */
        lz_rtree_leaf_t* expected = NULL;
        if (!atomic_compare_exchange_strong_explicit(&g_rtree_root.leaves[l1_idx], 
                                                     &expected, new_leaf, 
                                                     memory_order_release, 
                                                     memory_order_acquire)) {
            /* Race lost: Another thread initialized the leaf first. 'expected' holds the winner. */
            munmap(new_leaf, sizeof(lz_rtree_leaf_t));
            leaf = expected;
        } else {
            /* Race won: We successfully mapped the new leaf. */
            leaf = new_leaf;
        }
    }

    atomic_store_explicit(&leaf->entries[l2_idx], metadata, memory_order_release);
}

void lz_rtree_clear(uintptr_t chunk_addr) {
    lz_rtree_set(chunk_addr, NULL);
}