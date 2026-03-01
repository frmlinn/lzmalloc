/**
 * @file tlh.h
 * @brief Thread-Local Heap (TLH). Main allocation engine with Batching and Obfuscation.
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

/* ========================================================================= *
 * Batching and Free-List Structures
 * ========================================================================= */

/**
 * @brief Free list node embedded directly inside inactive memory.
 */
typedef struct lz_free_node_s {
    struct lz_free_node_s* next; // Stored OBFUSCATED (local) or CLEAR (remote)
} lz_free_node_t;

/**
 * @brief Outgoing mailbox for Simple Batching of remote frees (Phase 3.4).
 */
typedef struct {
    struct lz_tlh_s* target_tlh; // 8 bytes
    lz_free_node_t* head;        // 8 bytes
    lz_free_node_t* tail;        // 8 bytes
    uint32_t count;              // 4 bytes
    // Implicit 4-byte padding here for 8-byte alignment
} lz_batch_t;

/* ========================================================================= *
 * TLH Structures
 * ========================================================================= */

/**
 * @brief Internal bin managing Slabs for a specific Size Class.
 */
typedef struct {
    lz_slab_t* current_slab; 
    lz_slab_t* partial_list; 
    lz_slab_t* full_list;    
} lz_bin_t;

/**
 * @brief The Thread-Local Heap.
 * Aligned to 64 bytes to prevent False Sharing across CPU cores.
 */
typedef struct LZ_CACHE_ALIGNED lz_tlh_s {
    // --- CACHE LINE 1: Extremely Hot Data (Fast-Path) ---
    lz_bin_t bins[LZ_MAX_SIZE_CLASSES]; // (Large array, crosses multiple lines)

    // --- Atomic Variables and Pointers (Natural 8-byte alignment) ---
    _Atomic(lz_free_node_t*) remote_free_head;
    lz_batch_t outgoing_batch;

    // [PACKED SECTION] 32-bit (4-byte) variables grouped with zero padding
    uint32_t thread_id;               // 4 bytes
    uint32_t is_zombie;               // 4 bytes
    uint32_t local_bytes_alloc_batch; // 4 bytes (Phase 5.3 Telemetry)
    uint32_t local_bytes_free_batch;  // 4 bytes (Phase 5.3 Telemetry)
    // Total block: 16 bytes exactly.

    // --- 64-bit Variables and Pointers ---
    size_t bytes_allocated;         // 8 bytes
    struct lz_tlh_s* next_zombie;   // 8 bytes
    lz_stat_slot_t* stat_slot;      // 8 bytes (Pointer to SHM Matrix)
} lz_tlh_t;

/* ========================================================================= *
 * TLH Public API
 * ========================================================================= */

void lz_tlh_init(lz_tlh_t* tlh, uint32_t tid);
void* lz_tlh_alloc(lz_tlh_t* tlh, size_t size);
void lz_tlh_free(lz_tlh_t* tlh, void* ptr);
void lz_tlh_flush_outgoing_batch(lz_tlh_t* tlh);
void lz_tlh_reap(lz_tlh_t* tlh);

#endif // LZ_TLH_H