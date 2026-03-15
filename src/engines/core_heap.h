/**
 * @file core_heap.h
 * @brief Physical Core-Affined Heap Architecture.
 * @details Implements a per-core memory isolation strategy to maximize 
 * cache locality and minimize cross-core synchronization. Includes a 
 * wait-free multiplexing mechanism for fallback arenas when a core is 
 * under high contention or preempted.
 */
#ifndef LZ_ENGINE_CORE_HEAP_H
#define LZ_ENGINE_CORE_HEAP_H

#include "compiler.h"
#include "chunk.h"
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

/** @brief Number of overflow arenas used when main core heaps are busy. */
#define LZ_FALLBACK_HEAPS 16
/** @brief Total heap count: physical cores + multiplexing fallbacks. */
#define LZ_TOTAL_HEAPS (LZ_MAX_CORES + LZ_FALLBACK_HEAPS)

/* Forward declarations for engine-specific routing */
struct lz_slab_bin_s;
struct lz_span_bin_s;

/**
 * @struct lz_free_node_s
 * @brief Intrusive node for linked-list based memory reclamation.
 * @details Embedded directly in the user payload during deallocation.
 */
typedef struct lz_free_node_s {
    struct lz_free_node_s* next; /**< Pointers are obfuscated for Safe Linking security. */
} lz_free_node_t;

/**
 * @struct lz_core_heap_t
 * @brief Thread-Safe Core-Isolated Heap.
 * @details Designed with strict cache-line separation (Mechanical Sympathy) 
 * to eliminate false sharing between local allocation paths and remote 
 * deallocation paths.
 */
typedef struct {
    /* --- HOT ZONE 1: Remote Deallocation (Cross-Core Contention) --- */
    
    /** * @brief MPSC (Multi-Producer Single-Consumer) Mailbox. 
     * @details Isolated on its own cache line to prevent remote producers 
     * from invalidating the local allocation hot-path. 
     */
    LZ_CACHELINE_ALIGNED uint64_t remote_free_mailbox; 
    
    /** * @brief Atomic Try-Lock for Main Core Heaps. 
     * @details Prevents reentrancy/preemption cycles on the same physical core. 
     */
    LZ_CACHELINE_ALIGNED _Atomic uint8_t is_busy; 
    
    /** @brief Traditional spinlock utilized exclusively for Fallback Arenas. */
    _Atomic uint8_t fallback_lock;

    /* --- HOT ZONE 2: Local Allocation (Core-Exclusive Path) --- */
    
    /** @brief Physical silicon core ID associated with this heap. */
    LZ_CACHELINE_ALIGNED uint32_t core_id;
    /** @brief Initialization flag for lazy bootstrapping. */
    uint32_t is_initialized;

    /** @brief Active medium-object spans (32KB - 1MB). */
    struct lz_span_bin_s* active_spans;
    /** @brief Binned small-object slabs (<= 32KB) for 88 size classes. */
    struct lz_slab_bin_s* active_slabs[88];

    /* --- COLD ZONE: Telemetry and Statistics --- */
    
    LZ_CACHELINE_ALIGNED size_t local_bytes_allocated;
    size_t local_bytes_freed;

} LZ_CACHELINE_ALIGNED lz_core_heap_t;

/** @brief Global heap matrix. Statically allocated to prevent bootstrap recursion. */
extern lz_core_heap_t g_core_heaps[LZ_TOTAL_HEAPS];

/**
 * @brief Reaps and processes all pending nodes in the MPSC mailbox.
 * @param heap Pointer to the heap instance to drain.
 * @note Must be called from the local thread owning the core or the fallback lock.
 */
void lz_core_heap_reap_mailbox(lz_core_heap_t* heap);

/**
 * @brief Dispatches a pointer for asynchronous deallocation to a target core.
 * @param target_core_id The core ID that originally allocated the memory.
 * @param ptr Pointer to the memory block to be freed.
 * @details Uses atomic CAS to push the node into the target's wait-free mailbox.
 */
void lz_core_heap_remote_free(uint32_t target_core_id, void* ptr);

#endif /* LZ_ENGINE_CORE_HEAP_H */