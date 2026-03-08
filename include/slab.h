/**
 * @file slab.h
 * @brief Slab formatting and lifecycle definitions for the Posix engine.
 * A Slab formats a 2MB Chunk to serve objects of a specific Size Class.
 */

#ifndef LZ_SLAB_H
#define LZ_SLAB_H

#include "common.h"

/* ========================================================================= *
 * Slab Metadata Structures
 * ========================================================================= */

/**
 * @struct lz_slab_t
 * @brief Central tracking structure for a formatted Slab.
 * @note Resides directly after the 64-byte Chunk header to maximize cache locality.
 */
typedef struct lz_slab_s {
    struct lz_slab_s* next;     /**< Doubly linked list pointer (next) */
    struct lz_slab_s* prev;     /**< Doubly linked list pointer (prev) */
    
    void* memory_base;          /**< Start boundary of usable payload data */
    void* memory_end;           /**< End boundary of the Huge Page Chunk */
    
    void* bump_ptr;             /**< Allocation cursor for lazy initialization */
    void* free_list_head;       /**< Singly linked list of freed objects within this slab */
    
    uint32_t size_class;        /**< Target Size Class index (0 to LZ_MAX_SIZE_CLASSES-1) */
    uint32_t block_size;        /**< Object footprint in bytes (cached to avoid recalculation) */
    uint32_t max_objects;       /**< Total geometric object capacity of the slab */
    uint32_t used_objects;      /**< Count of currently live objects */
} lz_slab_t;

/* ========================================================================= *
 * Slab Lifecycle API
 * ========================================================================= */

/* Forward declaration of TLH to prevent circular dependencies */
struct lz_tlh_s;

/**
 * @brief Fetches a blank Chunk from the VMM and formats it as a typed Slab.
 * @param tlh Pointer to the executing thread's Thread-Local Heap.
 * @param class_idx The target Size Class index.
 * @return Pointer to the newly initialized Slab structure.
 */
lz_slab_t* lz_slab_create(struct lz_tlh_s* tlh, uint32_t class_idx);

/**
 * @brief Teardown sequence for an empty Slab. Releases the Chunk to the VMM.
 * @param slab Pointer to the Slab slated for destruction.
 */
void lz_slab_destroy(lz_slab_t* slab);

#endif /* LZ_SLAB_H */