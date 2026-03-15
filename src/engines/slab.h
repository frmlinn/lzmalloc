/**
 * @file slab.h
 * @brief Slab Engine for Small Objects (<= 32KB).
 * @details Implements a hybrid bump-pointer and free-list strategy within 
 * 2MB superblocks. Designed for high cache locality and O(1) recycling.
 */
#ifndef LZ_ENGINE_SLAB_H
#define LZ_ENGINE_SLAB_H

#include "chunk.h"
#include "core_heap.h"

/**
 * @struct lz_slab_bin_s
 * @brief Slab Metadata Header.
 * @details Resides on the second cache line of the Chunk (Offset 0x40).
 */
typedef struct LZ_CACHELINE_ALIGNED lz_slab_bin_s {
    struct lz_slab_bin_s* next; /**< Intrusive link to the next partially full slab. */
    struct lz_slab_bin_s* prev; /**< Intrusive link to the previous slab. */

    uint8_t* bump_ptr;          /**< Cursor for fresh object allocation. */
    uint8_t* bump_limit;        /**< End of the slab's usable payload. */
    lz_free_node_t* free_list;  /**< Linked list of recycled objects (Safe Linked). */

    uint32_t block_size;        /**< Footprint of each object in this slab. */
    uint32_t size_class_idx;    /**< Routing index (0-87). */
    uint32_t used_objects;      /**< Count of live allocations. */
    
} lz_slab_bin_t;

/**
 * @brief Allocates a small object from the core-local heap.
 * @param heap Pointer to the core-affined heap.
 * @param size_class_idx Normalized size class.
 * @param block_size Aligned object size.
 * @return Pointer to user payload, or NULL.
 */
void* lz_slab_alloc_local(lz_core_heap_t* heap, uint32_t size_class_idx, uint32_t block_size);

/**
 * @brief Deallocates a small object back to the local slab.
 * @param heap Pointer to the core-affined heap.
 * @param chunk Associated metadata header.
 * @param ptr Pointer to the user payload to free.
 */
void lz_slab_free_local(lz_core_heap_t* heap, lz_chunk_t* chunk, void* ptr);

#endif /* LZ_ENGINE_SLAB_H */