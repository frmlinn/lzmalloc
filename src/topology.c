/**
 * @file topology.c
 * @brief Implementation of NUMA detection using native syscalls.
 */

#define _GNU_SOURCE // Required for Linux syscalls
#include "topology.h"

#ifdef __linux__
#include <unistd.h>
#include <sys/syscall.h>
#endif

/* ========================================================================= *
 * Global State and Pools (Static)
 * ========================================================================= */

// Total node count in the machine. Defaults to 1 (standard system).
static uint32_t g_numa_node_count = 1;

// Aligned pools array. One for each possible NUMA node.
static lz_numa_pool_t g_numa_pools[LZ_MAX_NUMA_NODES] LZ_CACHE_ALIGNED = {0};

/* ========================================================================= *
 * Thread-Local State (TLS)
 * ========================================================================= */

// Local cache to avoid calling getcpu on every allocation.
// Initialized to an invalid value (-1) to force a read on the first attempt.
static __thread int32_t tls_current_node = -1;

/* ========================================================================= *
 * Implementation
 * ========================================================================= */

void lz_topology_init(void) {
    // For simplicity and independence, we assume all systems have at least 1 NUMA node.
    // Exhaustive detection would require parsing /sys/devices/system/node/
    // We initialize safely to 1.
    
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
    // Fast-path: If we already know the node, return it directly.
    if (LZ_LIKELY(tls_current_node >= 0)) {
        return (uint32_t)tls_current_node;
    }

    // Slow-path: First time this thread requests its node.
#ifdef __linux__
    unsigned cpu, node;
    
    // SYS_getcpu is supported on Linux and uses vDSO, making it extremely fast.
    if (syscall(SYS_getcpu, &cpu, &node, NULL) == 0) {
        // Security validation to prevent overflowing our pool array
        if (node < LZ_MAX_NUMA_NODES) {
            tls_current_node = node;
            return node;
        }
    }
#endif

    // Fallback: If OS unsupported (macOS) or non-NUMA architecture, assign all to Node 0.
    tls_current_node = 0;
    return 0;
}

lz_numa_pool_t* lz_topology_get_pool(uint32_t node_id) {
    // Defensive design: If requested a node out of bounds, fallback to Node 0.
    if (LZ_UNLIKELY(node_id >= LZ_MAX_NUMA_NODES)) {
        return &g_numa_pools[0];
    }
    return &g_numa_pools[node_id];
}