/**
 * @file topology.c
 * @brief Implementation of NUMA detection using native syscalls via vDSO.
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
 * Implementation
 * ========================================================================= */

void lz_topology_init(void) {
    // For simplicity and independence, we assume all systems have at least 1 NUMA node.
    // Exhaustive detection would require parsing /sys/devices/system/node/
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
    
    // SYS_getcpu is supported on Linux and uses vDSO (Virtual dynamically linked shared object).
    // This makes it essentially a user-space memory read (~1-2ns).
    // By calling it dynamically every time, we are immune to the "NUMA Migration Trap"
    // where the OS scheduler aggressively moves a thread to a different physical CPU socket.
    if (LZ_LIKELY(syscall(SYS_getcpu, &cpu, &node, NULL) == 0)) {
        // Security validation to prevent overflowing our pool array
        if (LZ_LIKELY(node < LZ_MAX_NUMA_NODES)) {
            return node;
        }
    }
#endif

    // Fallback: If OS unsupported (macOS/FreeBSD) or non-NUMA architecture, assign all to Node 0.
    return 0;
}

lz_numa_pool_t* lz_topology_get_pool(uint32_t node_id) {
    // Defensive design: If requested a node out of bounds, fallback to Node 0.
    if (LZ_UNLIKELY(node_id >= LZ_MAX_NUMA_NODES)) {
        return &g_numa_pools[0];
    }
    return &g_numa_pools[node_id];
}