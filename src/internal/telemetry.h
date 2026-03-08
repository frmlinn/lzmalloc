/**
 * @file telemetry.h
 * @brief Shared Memory (SHM) Telemetry Matrix for Zero-Overhead observability.
 */

#ifndef LZ_TELEMETRY_H
#define LZ_TELEMETRY_H

#include "common.h"

/* ========================================================================= *
 * Telemetry Memory Map Layout
 * ========================================================================= */

/** @brief Dedicated index slot for the global Virtual Memory Manager (VMM). */
#define LZ_VMM_SLOT_IDX 0

/** @brief Base index where Thread-Local Heap (TLH) telemetry slots begin. */
#define LZ_THREAD_SLOT_BASE_IDX 1 

/* ========================================================================= *
 * Slot Structure
 * ========================================================================= */

/**
 * @struct lz_stat_counters_t
 * @brief Internal payload of statistics to calculate exact padding requirements.
 */
typedef struct {
    _Atomic(uint64_t) bytes_requested; /**< Useful bytes requested by the application */
    _Atomic(int64_t)  bytes_allocated; /**< Actual physical bytes consumed (including metadata) */
    _Atomic(int64_t)  active_objects;  /**< Current count of live allocations/chunks */
    _Atomic(uint64_t) events;          /**< Monotonic counter for VMM expansions/GC runs */
} lz_stat_counters_t;

/**
 * @struct lz_stat_slot_t
 * @brief Individual metric slot for lock-free atomic statistics.
 * Explicitly padded to match the hardware cache line size to mathematically prevent False Sharing.
 */
typedef struct LZ_CACHE_ALIGNED {
    lz_stat_counters_t data;
    
    /** @brief Dynamic byte padding to saturate the cache line safely across all architectures. */
    uint8_t _padding[LZ_CACHE_LINE_SIZE - sizeof(lz_stat_counters_t)];
} lz_stat_slot_t;

/* ========================================================================= *
 * Public API
 * ========================================================================= */

/**
 * @brief Initializes the SHM matrix in /dev/shm.
 * @note Only activates if the 'LZMALLOC_TELEMETRY' environment variable is present.
 * Uses strict allocation-free logic to prevent cyclic bootstrap dependencies.
 */
void lz_telemetry_init(void);

/**
 * @brief Fetches a pointer to a specific telemetry slot.
 * @param index The requested slot index.
 * @return Pointer to the slot, or NULL if telemetry is disabled or out of bounds.
 */
lz_stat_slot_t* lz_telemetry_get_slot(uint32_t index);

#endif /* LZ_TELEMETRY_H */