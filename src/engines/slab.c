/**
 * @file slab.c
 * @brief Implementation of the Slab initialization and destruction lifecycle.
 */

#include "slab.h"
#include "chunk.h"
#include "vmm.h"
#include "rtree.h"
#include "sizes.h"
//#include "tlh.h" 
#include <stddef.h>

/* ========================================================================= *
 * Slab Creation
 * ========================================================================= */

lz_slab_t* lz_slab_create(struct lz_tlh_s* tlh, uint32_t class_idx) {
    /* 1. Request a pristine, memory-aligned Chunk from the VMM */
    lz_chunk_header_t* chunk = lz_vmm_alloc_chunk();
    if (LZ_UNLIKELY(!chunk)) {
        return NULL; 
    }

    /* 2. Configure Chunk Shielding and Metadata */
    chunk->owning_tlh = tlh;

    chunk->chunk_type = LZ_CHUNK_TYPE_SLAB;
    
    /* Checksum is calculated AFTER setting all metadata fields to ensure validity */
    chunk->checksum = lz_calc_checksum(chunk);
    
    /* Register this Chunk in the Radix Tree for O(1) lock-free resolution in free() */
    lz_rtree_set((uintptr_t)chunk, chunk);

    /* 3. Position the slab metadata safely past the 64-byte chunk header */
    lz_slab_t* slab = (lz_slab_t*)((char*)chunk + LZ_CACHE_LINE_SIZE);

    /* 4. Calculate payload boundaries. 
     * Shift data_start up to the nearest cache line to guarantee alignment. 
     */
    uintptr_t data_start = LZ_ALIGN_UP((uintptr_t)slab + sizeof(lz_slab_t), LZ_CACHE_LINE_SIZE);
    uintptr_t data_end   = (uintptr_t)chunk + LZ_HUGE_PAGE_SIZE;

    /* 5. Initialize internal Slab state */
    slab->next = NULL;
    slab->prev = NULL;
    slab->memory_base = (void*)data_start;
    slab->memory_end  = (void*)data_end;
    
    /* Lazy Initialization Strategy: Defer memory touching until absolutely necessary. */
    slab->bump_ptr       = (void*)data_start;
    slab->free_list_head = NULL;

    slab->size_class = (uint32_t)class_idx;
    slab->block_size = (uint32_t)lz_class_to_size(class_idx);
    
    /* Compute physical capacity geometrically */
    slab->max_objects  = (uint32_t)((data_end - data_start) / slab->block_size);
    slab->used_objects = 0;

    return slab;
}

/* ========================================================================= *
 * Slab Destruction
 * ========================================================================= */

void lz_slab_destroy(lz_slab_t* slab) {
    /* Resolve the origin address of the Chunk using bitwise mask against the Huge Page boundary. */
    uintptr_t chunk_mask = ~(((uintptr_t)1 << LZ_CHUNK_SHIFT) - 1);
    lz_chunk_header_t* chunk = (lz_chunk_header_t*)((uintptr_t)slab & chunk_mask);

    /* Security Layer: Validate the metadata hash before passing it back to the VMM.
     * Thwarts Use-After-Free metadata overwrites. 
     */
    if (LZ_UNLIKELY(chunk->checksum != lz_calc_checksum(chunk))) {
        return; /* Silent failure in release. Typically triggers a fatal abort in debug. */
    }

    /* Purge the routing registry from the Radix Tree */
    lz_rtree_clear((uintptr_t)chunk);

    /* Delegate the actual memory reclamation back to the Virtual Memory Manager */
    lz_vmm_free_chunk(chunk);
}