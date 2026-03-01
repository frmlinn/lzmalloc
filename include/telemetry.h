/**
 * @file telemetry.h
 * @brief Definition of the Shared Memory (SHM) Telemetry Matrix for Zero-Overhead observability.
 */

#ifndef LZ_TELEMETRY_H
#define LZ_TELEMETRY_H

#include "common.h"
#include <stdint.h>
#include <stdatomic.h>

/* ========================================================================= *
 * Telemetry Memory Map Layout
 * ========================================================================= */

// Note: LZ_MAX_TELEMETRY_SLOTS and LZ_LSM_SLOTS_COUNT are provided by lz_config.h
#define LZ_VMM_SLOT_IDX         0
#define LZ_LSM_SLOT_BASE_IDX    1 
#define LZ_THREAD_SLOT_BASE_IDX (LZ_LSM_SLOT_BASE_IDX + LZ_LSM_SLOTS_COUNT) 

/* ========================================================================= *
 * Slot Structure
 * ========================================================================= */

/**
 * @brief Individual metric slot.
 * Forced to 64 bytes (Cache Line Size) to prevent False Sharing across CPU cores
 * on the memory bus during high-frequency concurrent atomic updates.
 */
typedef struct LZ_CACHE_ALIGNED {
    _Atomic uint64_t bytes_requested; // Useful bytes requested by the user (LSM)
    _Atomic int64_t bytes_allocated;  // Actual bytes consumed (includes overhead/metadata)
    _Atomic int64_t active_objects;   // Count of live objects, regions, or Chunks
    _Atomic uint64_t events;          // Special events (GC runs, VMM expansions)
    
    // Implicit compiler padding guarantees 64 bytes alignment
} lz_stat_slot_t;

/* ========================================================================= *
 * Public API
 * ========================================================================= */

/**
 * @brief Initializes the SHM file in /dev/shm. 
 * Only active if the LZMALLOC_TELEMETRY environment variable is set.
 */
void lz_telemetry_init(void);

/**
 * @brief Retrieves a pointer to a specific slot in the telemetry matrix.
 * @param index The requested slot index.
 * @return Pointer to the slot, or NULL if telemetry is disabled or out of bounds.
 */
lz_stat_slot_t* lz_telemetry_get_slot(uint32_t index);

#endif // LZ_TELEMETRY_H