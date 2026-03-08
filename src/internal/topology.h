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

/* ========================================================================= *
 * Core Data Structures
 * ========================================================================= */

/**
 * @struct lz_numa_pool_t
 * @brief Lock-free Chunk pool isolated per NUMA node to guarantee local allocations.
 * * Explicitly padded to LZ_CACHE_LINE_SIZE to mathematically eliminate 
 * cross-node False Sharing during array traversal.
 */
typedef struct LZ_CACHE_ALIGNED {
    _Atomic(struct lz_chunk_header_s*) free_chunks; /**< Treiber Stack top pointer */
    _Atomic(size_t) available_count;                /**< Current chunks available */
    
    /* Hardware sympathy padding to fill the L1/L2 cache line */
    uint8_t _padding[LZ_CACHE_LINE_SIZE - sizeof(_Atomic(struct lz_chunk_header_s*)) - sizeof(_Atomic(size_t))];
} lz_numa_pool_t;

/* ========================================================================= *
 * Topology Module Public API
 * ========================================================================= */

/**
 * @brief Bootstraps the system topology detection mechanism.
 * @note Must be invoked exactly once during the allocator's global initialization.
 */
void lz_topology_init(void);

/**
 * @brief Retrieves the total number of operational NUMA nodes detected.
 * @return Node count (defaults to 1 for UMA architectures).
 */
uint32_t lz_topology_get_node_count(void);

/**
 * @brief Dynamically fetches the NUMA node ID of the currently executing thread.
 * Utilizes vDSO routing to prevent the NUMA Migration Trap with near-zero latency.
 * * @return The NUMA node ID (0 to LZ_MAX_NUMA_NODES - 1).
 */
uint32_t lz_get_current_node(void);

/**
 * @brief Fetches the dedicated Chunk pool for a specific NUMA node.
 * * @param node_id The target NUMA node ID.
 * @return A pointer to the node's isolated lz_numa_pool_t.
 */
lz_numa_pool_t* lz_topology_get_pool(uint32_t node_id);

#endif /* LZ_TOPOLOGY_H */