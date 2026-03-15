/**
 * @file slab.c
 * @brief Implementation of the Slab Engine for Small Objects (<= 32KB).
 * @details Manages 2MB superblocks partitioned into fixed-size blocks. 
 * High performance is achieved via a hybrid Bump-Pointer and intrusive Free-List 
 * strategy. Includes "Active Unlinking" to ensure O(1) allocation bounds 
 * by removing exhausted slabs from the search path.
 */
#include "slab.h"
#include "vmm.h"
#include "pagemap.h"
#include "security.h"
#include "lz_log.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Allocates a small object from a core-exclusive slab.
 * @details Prioritizes recycling from the intrusive free-list to maximize 
 * temporal cache locality before advancing the bump-pointer.
 * @param heap Pointer to the requesting thread's current core-local heap.
 * @param size_class_idx The normalized index (0-87) for the size class.
 * @param block_size The actual byte footprint of the object.
 * @return Pointer to user payload, or NULL if the VMM is exhausted.
 */
void* lz_slab_alloc_local(lz_core_heap_t* heap, uint32_t size_class_idx, uint32_t block_size) {
    lz_slab_bin_t* slab = heap->active_slabs[size_class_idx];

    if (LZ_LIKELY(slab != NULL)) {
        void* ptr = NULL;

        /* 1. Fast-Path: O(1) Immediate Recycling via Intrusive Free List */
        if (LZ_LIKELY(slab->free_list != NULL)) {
            lz_free_node_t* node = slab->free_list;
            /* De-obfuscate the next pointer using Safe Linking entropy */
            slab->free_list = (lz_free_node_t*)lz_ptr_obfuscate(node->next, (void**)&node->next);
            ptr = (void*)node;
        }
        /* 2. Fast-Path: Sequential Bump Pointer (Mechanical Sympathy) */
        else if (LZ_LIKELY(slab->bump_ptr + slab->block_size <= slab->bump_limit)) {
            ptr = (void*)slab->bump_ptr;
            slab->bump_ptr += slab->block_size;
        }

        slab->used_objects++;

        /**
         * @brief Active Unlinking Mechanism.
         * @details If the slab is mathematically full (no free nodes and no bump 
         * space), it is removed from the core's active list. This guarantees 
         * that the head of the list is always ready to serve a request in O(1).
         */
        bool out_of_bump = (slab->bump_ptr + slab->block_size > slab->bump_limit);
        if (LZ_UNLIKELY(slab->free_list == NULL && out_of_bump)) {
            heap->active_slabs[size_class_idx] = slab->next;
            if (slab->next) {
                slab->next->prev = NULL;
            }
            slab->next = NULL;
            slab->prev = NULL;
        }

        return ptr;
    }

    /* 4. Slow-Path: Provision physical backing from the Global VMM Arena */
    lz_chunk_t* chunk = lz_vmm_alloc_chunk(heap->core_id);
    if (LZ_UNLIKELY(!chunk)) return NULL;

    chunk->chunk_type = LZ_CHUNK_TYPE_SLAB;
    /* Map the 2MB region to this header in the Flat Pagemap for O(1) resolution */
    lz_pagemap_set_slow(chunk, chunk);

    /* Initialize Slab Metadata on the second cache line of the Chunk */
    lz_slab_bin_t* new_slab = (lz_slab_bin_t*)((uint8_t*)chunk + LZ_CACHE_LINE_SIZE);
    uintptr_t payload_base = (uintptr_t)chunk + (LZ_CACHE_LINE_SIZE * 2);
    
    new_slab->bump_ptr = (uint8_t*)payload_base;
    new_slab->bump_limit = (uint8_t*)chunk + LZ_HUGE_PAGE_SIZE;
    new_slab->free_list = NULL;
    
    new_slab->block_size = block_size;
    new_slab->size_class_idx = size_class_idx;
    new_slab->used_objects = 0;

    /* Link into the Core Heap's active list (O(1) Head Insertion) */
    new_slab->next = heap->active_slabs[size_class_idx];
    new_slab->prev = NULL;
    if (heap->active_slabs[size_class_idx]) {
        heap->active_slabs[size_class_idx]->prev = new_slab;
    }
    heap->active_slabs[size_class_idx] = new_slab;

    LZ_DEBUG("Slab: Provisioned Chunk (Class: %u, Block: %u)", size_class_idx, block_size);

    /* Serve the first object from the new chunk */
    void* ptr = (void*)new_slab->bump_ptr;
    new_slab->bump_ptr += new_slab->block_size;
    new_slab->used_objects++;

    return ptr;
}

/**
 * @brief Deallocates a small object back to the local slab.
 * @details Uses Safe Linking to obfuscate the free-list pointers and performs 
 * "Active Relinking" if the slab transitions from Full to Partial.
 * @param heap Current core-local heap.
 * @param chunk Owning metadata header.
 * @param ptr Pointer to be reclaimed.
 */
void lz_slab_free_local(lz_core_heap_t* heap, lz_chunk_t* chunk, void* ptr) {
    lz_slab_bin_t* slab = (lz_slab_bin_t*)((uint8_t*)chunk + LZ_CACHE_LINE_SIZE);
    lz_free_node_t* node = (lz_free_node_t*)ptr;
    
    /* Determine state prior to release for relinking logic */
    bool out_of_bump = (slab->bump_ptr + slab->block_size > slab->bump_limit);
    bool was_full = (slab->free_list == NULL && out_of_bump);

    /* Intrusive Free List Push with XOR-masking (Safe Linking) */
    node->next = (lz_free_node_t*)lz_ptr_obfuscate(slab->free_list, (void**)&node->next);
    slab->free_list = node;
    slab->used_objects--;

    /**
     * @brief Active Relinking.
     * @details If this slab was previously "invisible" because it was full, 
     * it is re-inserted into the active list to make its new free slot usable.
     */
    if (LZ_UNLIKELY(was_full)) {
        uint32_t target_idx = slab->size_class_idx;
        slab->next = heap->active_slabs[target_idx];
        slab->prev = NULL;
        if (heap->active_slabs[target_idx]) {
            heap->active_slabs[target_idx]->prev = slab;
        }
        heap->active_slabs[target_idx] = slab;
        return; 
    }

    /**
     * @brief Chunk Reclamation & Hysteresis.
     * @details Returns the 2MB chunk to the VMM if it becomes entirely empty. 
     * Retains the slab if it is the only one left for its size class (Hysteresis).
     */
    if (LZ_UNLIKELY(slab->used_objects == 0)) {
        uint32_t target_idx = slab->size_class_idx;
        
        /* Check if this is the last available slab for this class to prevent thrashing */
        if (heap->active_slabs[target_idx] == slab && slab->next == NULL) {
            return; 
        }

        /* Standard doubly-linked list removal */
        if (slab->prev) {
            slab->prev->next = slab->next;
        } else {
            heap->active_slabs[target_idx] = slab->next;
        }
        
        if (slab->next) {
            slab->next->prev = slab->prev;
        }

        /* Clear Pagemap entry and return to Global Treiber Stack */
        lz_pagemap_set_slow(chunk, NULL);
        lz_vmm_free_chunk(chunk);
        LZ_DEBUG("Slab: Chunk destroyed and returned to VMM (Class: %u)", target_idx);
    }
}