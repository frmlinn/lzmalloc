/**
 * @file slab.h
 * @brief Slab definitions for the general engine.
 * A Slab packages objects of the exact same size (Size Class).
 */

#ifndef LZ_SLAB_H
#define LZ_SLAB_H

#include "common.h"

/* ========================================================================= *
 * Slab Metadata Structures
 * ========================================================================= */

/**
 * @brief Slab Header.
 * In lzmalloc V2, this structure is NOT embedded with the user data.
 * It lives within the thread's Thread-Local Heap (TLH), ensuring maximum cache locality.
 */
typedef struct lz_slab_s {
    struct lz_slab_s* next;     // Linked list of slabs (partial, full, free)
    struct lz_slab_s* prev;
    
    void* memory_base;          // Start of usable data area
    void* memory_end;           // End boundary of the OS Chunk
    
    void* bump_ptr;             // Pointer for lazy initialization
    void* free_list_head;       // Singly linked list of free objects WITHIN this slab
    
    // Extracted uint32_t to prevent overflow on very small objects inside Huge Pages
    uint32_t size_class;        // Size class index (0 to LZ_MAX_SIZE_CLASSES-1)
    uint32_t block_size;        // Actual size of each object in bytes (cached for speed)
    uint32_t max_objects;       // Total object capacity in this slab
    uint32_t used_objects;      // Currently allocated objects
} lz_slab_t;

/* ========================================================================= *
 * Slab Lifecycle API
 * ========================================================================= */

/* Forward declaration of TLH to prevent circular dependencies with tlh.h */
struct lz_tlh_s;

/**
 * @brief Requests a Chunk from the VMM and formats it as a Slab.
 * @param tlh Pointer to the owning Thread-Local Heap.
 * @param class_idx The target Size Class index.
 * @return Pointer to the newly formatted Slab.
 */
lz_slab_t* lz_slab_create(struct lz_tlh_s* tlh, uint32_t class_idx);

/**
 * @brief Destroys an empty Slab and returns its Chunk back to the VMM.
 * @param slab Pointer to the Slab to destroy.
 */
void lz_slab_destroy(lz_slab_t* slab);

#endif // LZ_SLAB_H