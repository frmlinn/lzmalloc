/**
 * @file slab.c
 * @brief Implementation of the Slab initialization and destruction lifecycle.
 */

#include "slab.h"
#include "chunk.h"
#include "vmm.h"
#include "rtree.h"
#include "sizes.h"
#include <stddef.h>

/* ========================================================================= *
 * Slab Creation
 * ========================================================================= */

lz_slab_t* lz_slab_create(struct lz_tlh_s* tlh, uint32_t class_idx) {
    lz_chunk_header_t* chunk = lz_vmm_alloc_chunk();
    if (LZ_UNLIKELY(!chunk)) {
        return NULL; 
    }

    /* Configure Chunk Shielding and Metadata */
    chunk->owning_tlh = tlh;
    chunk->chunk_type = LZ_CHUNK_TYPE_SLAB;
    
    /* Strict Integrity: Checksum MUST be evaluated last */
    chunk->checksum = lz_calc_checksum(chunk);
    
    lz_rtree_set((uintptr_t)chunk, chunk);

    /* Position the slab metadata safely past the 64-byte chunk header */
    lz_slab_t* slab = (lz_slab_t*)((char*)chunk + LZ_CACHE_LINE_SIZE);

    /* Safely compute boundaries to prevent misaligned reads */
    uintptr_t data_start = LZ_ALIGN_UP((uintptr_t)slab + sizeof(lz_slab_t), LZ_CACHE_LINE_SIZE);
    uintptr_t data_end   = (uintptr_t)chunk + LZ_HUGE_PAGE_SIZE;

    slab->next = NULL;
    slab->prev = NULL;
    slab->memory_base = (void*)data_start;
    slab->memory_end  = (void*)data_end;
    
    /* Lazy Initialization Strategy */
    slab->bump_ptr       = (void*)data_start;
    slab->free_list_head = NULL;

    slab->size_class = (uint32_t)class_idx;
    slab->block_size = (uint32_t)lz_class_to_size(class_idx);
    
    slab->max_objects  = (uint32_t)((data_end - data_start) / slab->block_size);
    slab->used_objects = 0;

    return slab;
}

/* ========================================================================= *
 * Slab Destruction
 * ========================================================================= */

void lz_slab_destroy(lz_slab_t* slab) {
    uintptr_t chunk_mask = ~(((uintptr_t)1 << LZ_CHUNK_SHIFT) - 1);
    lz_chunk_header_t* chunk = (lz_chunk_header_t*)((uintptr_t)slab & chunk_mask);

    if (LZ_UNLIKELY(chunk->checksum != lz_calc_checksum(chunk))) {
        LZ_FATAL("Slab destruction blocked: Chunk metadata corruption detected.");
        return; 
    }

    lz_rtree_clear((uintptr_t)chunk);
    lz_vmm_free_chunk(chunk);
}