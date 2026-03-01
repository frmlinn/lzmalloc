/**
 * @file topology.h
 * @brief Hardware topology detection and NUMA pool definition for lzmalloc V2.
 */

#ifndef LZ_TOPOLOGY_H
#define LZ_TOPOLOGY_H

#include "common.h"

/* ========================================================================= *
 * Forward Declarations
 * ========================================================================= */

struct lz_chunk_header_s;

/**
 * @brief Exclusive Chunk pool for a specific NUMA node.
 */
typedef struct LZ_CACHE_ALIGNED {
    _Atomic(struct lz_chunk_header_s*) free_chunks; // Treiber Stack 
    _Atomic(size_t) available_count;
} lz_numa_pool_t;

/* ========================================================================= *
 * Topology Module Public API
 * ========================================================================= */

/**
 * @brief Initializes the system topology.
 * Must be called exactly once during allocator bootstrap (e.g., via constructor).
 * Discovers how many actual NUMA nodes the machine has.
 */
void lz_topology_init(void);

/**
 * @brief Gets the total number of detected NUMA nodes.
 * @return Node count (minimum 1 for UMA systems).
 */
uint32_t lz_topology_get_node_count(void);

/**
 * @brief Gets the NUMA node ID where the calling thread is currently executing.
 * Uses a Thread-Local Storage (TLS) cache to avoid multiple syscall overheads.
 * @return Node ID (0 to LZ_MAX_NUMA_NODES - 1).
 */
uint32_t lz_get_current_node(void);

/**
 * @brief Gets a pointer to the Chunk pool of the specified node.
 * @param node_id The NUMA node ID.
 * @return Pointer to the corresponding lz_numa_pool_t.
 */
lz_numa_pool_t* lz_topology_get_pool(uint32_t node_id);

#endif // LZ_TOPOLOGY_H