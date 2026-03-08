/**
 * @file tlh.h
 * @brief Thread-Local Heap (TLH). Main allocation engine with Batching and Obfuscation.
 * @details Implements the Layer 5 unified interface for Slabs and Spans, ensuring 
 * strict cache-line alignment to prevent False Sharing in multi-core NUMA topologies.
 */

#ifndef LZ_TLH_H
#define LZ_TLH_H

#include "common.h"
#include "sizes.h"
#include "telemetry.h"
#include <stdatomic.h>

/* Forward declarations to prevent circular dependencies */
struct lz_slab_s; 
typedef struct lz_slab_s lz_slab_t;

struct lz_span_s;
typedef struct lz_span_s lz_span_t;

/* ========================================================================= *
 * Batching and Free-List Structures
 * ========================================================================= */

/**
 * @struct lz_free_node_t
 * @brief Intrusive free-list node embedded directly inside inactive payload memory.
 */
typedef struct lz_free_node_s {
    struct lz_free_node_s* next; /**< OBFUSCATED (local thread) or CLEAR (remote thread) */
} lz_free_node_t;

/**
 * @struct lz_batch_t
 * @brief Outgoing mailbox for Simple Batching of cross-thread memory frees.
 * @note Strictly padded to 32 bytes to prevent unaligned atomic memory accesses.
 */
typedef struct {
    struct lz_tlh_s* target_tlh; /**< 8 bytes: Destination Thread-Local Heap */
    lz_free_node_t* head;        /**< 8 bytes: Start of the linked list batch */
    lz_free_node_t* tail;        /**< 8 bytes: End of the linked list batch */
    uint32_t count;              /**< 4 bytes: Number of pointers in the current batch */
    uint32_t _padding;           /**< 4 bytes: Explicit padding for perfect 32-byte alignment */
} lz_batch_t;

/* ========================================================================= *
 * TLH Structures
 * ========================================================================= */

/**
 * @struct lz_bin_t
 * @brief Internal routing bin managing the lifecycle of Slabs for a specific Size Class.
 */
typedef struct {
    lz_slab_t* current_slab; /**< Active Slab for bump allocation / fast free-list */
    lz_slab_t* partial_list; /**< Slabs with at least one free slot */
    lz_slab_t* full_list;    /**< Exhausted Slabs (Zero free slots) */
} lz_bin_t;

/**
 * @struct lz_tlh_t
 * @brief The Central Thread-Local Heap.
 * @warning Must be mapped dynamically or via TLS to guarantee LZ_CACHE_ALIGNED boundaries.
 */
typedef struct LZ_CACHE_ALIGNED lz_tlh_s {
    /* --- CACHE LINE 1 & BEYOND: Extremely Hot Data (Slab Fast-Path) --- */
    lz_bin_t bins[LZ_MAX_SIZE_CLASSES]; 
    
    /* --- Medium Object Engine Hook (Spans) --- */
    lz_span_t* active_spans;

    /* --- Atomic Variables and Pointers (Natural 8-byte alignment) --- */
    _Atomic(lz_free_node_t*) remote_free_head;
    lz_batch_t outgoing_batch;

    /* --- [PACKED SECTION] 32-bit variables logically grouped to save space --- */
    uint32_t thread_id;               
    uint32_t is_zombie;               
    uint32_t local_bytes_alloc_batch; 
    uint32_t local_bytes_free_batch;  
    /* Total packed block: exactly 16 bytes. No implicit compiler padding needed. */

    /* --- 64-bit Variables and Pointers --- */
    size_t bytes_allocated;         
    struct lz_tlh_s* next_zombie;   
    lz_stat_slot_t* stat_slot;      
} lz_tlh_t;

/* ========================================================================= *
 * TLH Public API
 * ========================================================================= */

/**
 * @brief Initializes a newly provisioned Thread-Local Heap.
 * @param tlh Pointer to the TLH memory region.
 * @param tid Monotonically increasing Thread ID assigned by the global router.
 */
void lz_tlh_init(lz_tlh_t* tlh, uint32_t tid);

/**
 * @brief Primary allocation router within the thread boundary.
 * @param tlh The calling thread's TLH.
 * @param size Requested allocation size in bytes.
 * @return Pointer to usable memory, or NULL on unrecoverable exhaustion.
 */
void* lz_tlh_alloc(lz_tlh_t* tlh, size_t size);

/**
 * @brief Universal free router. Handles both local and remote cross-thread frees.
 * @param tlh The executing thread's TLH (Not necessarily the memory owner).
 * @param ptr Pointer to the memory block being released.
 */
void lz_tlh_free(lz_tlh_t* tlh, void* ptr);

/**
 * @brief Flushes the pending remote free batch to the target thread using an atomic CAS.
 * @param tlh The executing thread's TLH.
 */
void lz_tlh_flush_outgoing_batch(lz_tlh_t* tlh);

/**
 * @brief Pulls pending remote frees from the atomic mailbox and processes them locally.
 * @param tlh The executing thread's TLH.
 */
void lz_tlh_reap(lz_tlh_t* tlh);

#endif /* LZ_TLH_H */