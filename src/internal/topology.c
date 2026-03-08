/**
 * @file topology.c
 * @brief Implementation of NUMA topology detection via vDSO syscalls.
 */

#define _GNU_SOURCE 
#include "topology.h"

#ifdef __linux__
#include <sched.h> /* Required for vDSO getcpu() */
#endif

/* ========================================================================= *
 * Global State and Pools (Static)
 * ========================================================================= */

/** @brief Global node count register, safely initialized to 1 for generic UMA. */
static uint32_t g_numa_node_count = 1;

/** @brief Statically allocated array of cache-aligned NUMA pools. */
static lz_numa_pool_t g_numa_pools[LZ_MAX_NUMA_NODES] LZ_CACHE_ALIGNED = {0};

/* ========================================================================= *
 * Implementation
 * ========================================================================= */

void lz_topology_init(void) {
    g_numa_node_count = 1;

    for (uint32_t i = 0; i < LZ_MAX_NUMA_NODES; ++i) {
        atomic_init(&g_numa_pools[i].free_chunks, NULL);
        atomic_init(&g_numa_pools[i].available_count, 0);
    }
}

uint32_t lz_topology_get_node_count(void) {
    return g_numa_node_count;
}

uint32_t lz_get_current_node(void) {
#ifdef __linux__
    unsigned cpu, node;
    
    /* * Routing through glibc's getcpu() utilizes the vDSO memory page 
     * mapped in user-space, avoiding a context switch to ring-0. 
     * Extremely fast (few nanoseconds) and prevents NUMA migration penalties.
     */
    if (LZ_LIKELY(getcpu(&cpu, &node) == 0)) {
        if (LZ_LIKELY(node < LZ_MAX_NUMA_NODES)) {
            return node;
        }
    }
#endif

    return 0;
}

lz_numa_pool_t* lz_topology_get_pool(uint32_t node_id) {
    if (LZ_UNLIKELY(node_id >= LZ_MAX_NUMA_NODES)) {
        return &g_numa_pools[0];
    }
    return &g_numa_pools[node_id];
}