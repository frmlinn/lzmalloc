/**
 * @file vmm.h
 * @brief Virtual Memory Manager (VMM) for lzmalloc V2.
 * Responsible for allocating and recycling 2MB Superblocks (Chunks).
 */

#ifndef LZ_VMM_H
#define LZ_VMM_H

#include "common.h"
#include "topology.h"
#include "chunk.h"

/* ========================================================================= *
 * VMM Public API
 * ========================================================================= */

/**
 * @brief Initializes the VMM and its dependencies (Topology).
 * Must be called once during allocator bootstrap.
 */
void lz_vmm_init(void);

/**
 * @brief Allocates a 2MB Chunk strictly aligned to a 2MB boundary.
 * Prioritizes the fast-cache of the current NUMA node. If empty, falls back to the OS.
 * @return Pointer to the aligned Chunk, or NULL if Out-Of-Memory (OOM).
 */
lz_chunk_header_t* lz_vmm_alloc_chunk(void);

/**
 * @brief Frees a 2MB Chunk.
 * Returns it to its original NUMA node cache. If the cache exceeds 
 * LZ_VMM_MAX_CACHED_CHUNKS, it aggressively unmaps the memory back to the OS.
 * @param chunk Pointer to the Chunk header.
 */
void lz_vmm_free_chunk(lz_chunk_header_t* chunk);

#endif // LZ_VMM_H