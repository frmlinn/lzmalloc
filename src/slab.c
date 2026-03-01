/**
 * @file slab.c
 * @brief Implementation of the Slab lifecycle.
 */

#include "slab.h"
#include "chunk.h"
#include "vmm.h"
#include "rtree.h"
#include "sizes.h"
#include "tlh.h" 
#include <stddef.h>

/* ========================================================================= *
 * Slab Creation
 * ========================================================================= */

lz_slab_t* lz_slab_create(struct lz_tlh_s* tlh, uint32_t class_idx) {
    // 1. Request a clean, aligned Chunk from the VMM
    lz_chunk_header_t* chunk = lz_vmm_alloc_chunk();
    if (LZ_UNLIKELY(!chunk)) return NULL; // Out Of Memory

    // 2. Configure Chunk Shielding (Layer 1/3)
    chunk->owning_tlh = tlh;
    chunk->is_lsm_region = 0;
    chunk->checksum = lz_calc_checksum(chunk);
    
    // Register this Chunk in the Radix Tree so free() can resolve it lock-free
    lz_rtree_set((uintptr_t)chunk, chunk);

    // 3. Position the lz_slab_t structure right after the 64-byte header
    lz_slab_t* slab = (lz_slab_t*)((char*)chunk + LZ_CACHE_LINE_SIZE);

    // 4. Calculate where real user data begins
    // Align data start to cache line boundary (64 bytes)
    uintptr_t data_start = LZ_ALIGN_UP((uintptr_t)slab + sizeof(lz_slab_t), LZ_CACHE_LINE_SIZE);
    uintptr_t data_end = (uintptr_t)chunk + LZ_HUGE_PAGE_SIZE;

    // 5. Initialize Slab metadata
    slab->next = NULL;
    slab->prev = NULL;
    slab->memory_base = (void*)data_start;
    slab->memory_end = (void*)data_end;
    
    // Lazy Initialization Magic: The free_list starts empty.
    slab->bump_ptr = (void*)data_start;
    slab->free_list_head = NULL;

    slab->size_class = (uint32_t)class_idx;
    slab->block_size = (uint32_t)lz_class_to_size(class_idx);
    
    // Calculate actual maximum capacity for this Chunk
    slab->max_objects = (uint32_t)((data_end - data_start) / slab->block_size);
    slab->used_objects = 0;

    return slab;
}

/* ========================================================================= *
 * Slab Destruction
 * ========================================================================= */

void lz_slab_destroy(lz_slab_t* slab) {
    // Get the start of the Chunk by masking out the lower bits
    // Use the dynamic LZ_CHUNK_SHIFT to create the mask safely
    uintptr_t chunk_mask = ~(((uintptr_t)1 << LZ_CHUNK_SHIFT) - 1);
    lz_chunk_header_t* chunk = (lz_chunk_header_t*)((uintptr_t)slab & chunk_mask);

    // For security, validate checksum before returning it to the OS/VMM
    if (LZ_UNLIKELY(chunk->checksum != lz_calc_checksum(chunk))) {
        // Metadata corruption detected. 
        // In production, this would trigger an abort() or fatal log.
        return; 
    }

    // Clear the record in the Radix Tree
    lz_rtree_clear((uintptr_t)chunk);

    // Return the Superblock to the VMM (which will put it in its NUMA cache)
    lz_vmm_free_chunk(chunk);
}