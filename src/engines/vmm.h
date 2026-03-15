/**
 * @file vmm.h
 * @brief Lock-Free Virtual Memory Manager.
 * @details Orchestrates the allocation and recycling of 2MB Superblocks (Chunks) 
 * using a Virtual Arena strategy. 
 */
#ifndef LZ_ENGINE_VMM_H
#define LZ_ENGINE_VMM_H

#include "chunk.h"
#include <stdint.h>

/** @brief Max number of formatted chunks retained in the local cache before RSS deflation. */
#define LZ_VMM_MAX_CACHED_CHUNKS 32

/**
 * @brief Bootstraps the Virtual Arena.
 * @note Must be called once during lz_system_init.
 */
void lz_vmm_init(void);

/**
 * @brief Allocates a raw 2MB Chunk.
 * @param core_id The requesting core ID to be stored in the chunk metadata.
 * @return Pointer to a hyper-aligned lz_chunk_t, or NULL on system OOM.
 */
lz_chunk_t* lz_vmm_alloc_chunk(uint32_t core_id);

/**
 * @brief Returns a Chunk to the global pool or caches it for reuse.
 * @param chunk Pointer to the chunk header to release.
 */
void lz_vmm_free_chunk(lz_chunk_t* chunk);

#endif /* LZ_ENGINE_VMM_H */