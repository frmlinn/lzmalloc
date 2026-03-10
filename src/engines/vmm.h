/**
 * @file vmm.h
 * @brief Virtual Memory Manager (VMM) for lzmalloc V2.
 * Responsible for allocating, caching, and recycling 2MB Superblocks (Chunks).
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
 * @brief Bootstraps the VMM and its underlying hardware topology dependencies.
 * @note Must be called exactly once during the global allocator initialization.
 */
void lz_vmm_init(void);

/**
 * @brief Allocates a 2MB/32MB Chunk strictly aligned to the huge page boundary.
 * Prioritizes the lock-free fast-cache of the current NUMA node. 
 * If the local cache is exhausted, it falls back to the Global Pool/OS.
 * * @return Pointer to the cleanly aligned Chunk header, or NULL on OS OOM.
 */
lz_chunk_header_t* lz_vmm_alloc_chunk(void);

/**
 * @brief Releases a Chunk back to the memory management subsystem.
 * Attempts to return it to the NUMA local cache for thermal hysteresis. 
 * If the cache exceeds LZ_VMM_MAX_CACHED_CHUNKS, it delegates to the Global Pool
 * and triggers active RSS deflation.
 * * @param chunk Pointer to the Chunk header to be freed.
 */
void lz_vmm_free_chunk(lz_chunk_header_t* chunk);

/**
 * @brief Actively deflates the RSS by purging all NUMA-local caches.
 * @details Extracts all idle Chunks across all nodes, explicitly calls 
 * madvise(MADV_DONTNEED) to release physical backing pages to the OS, 
 * and relocates the metadata to the global slow-path pool.
 */
void lz_vmm_purge_all_caches(void);

#endif /* LZ_VMM_H */