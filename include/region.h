/**
 * @file region.h
 * @brief Arena Allocator (LSM Engine). High-performance sequential memory allocation.
 */

#ifndef LZ_REGION_H
#define LZ_REGION_H

#include "common.h"
#include "chunk.h"
#include "telemetry.h"
#include <stdatomic.h>

/* ========================================================================= *
 * Region (Arena) Structures
 * ========================================================================= */

/**
 * @brief Header for massive memory requests (> 2MB).
 */
typedef struct lz_huge_node_s {
    struct lz_huge_node_s* next; // 8 bytes
    size_t size;                 // 8 bytes (Total: 16 bytes perfect alignment)
} lz_huge_node_t;

/**
 * @brief Region Control Structure.
 * Packed to maximize L1 Cache density.
 */
typedef struct LZ_CACHE_ALIGNED lz_region_s {
    // --- CACHE LINE 1: Fast-Path Pointers (Total 32 bytes) ---
    _Atomic(char*) bump_ptr;       // 8 bytes
    char* chunk_end;               // 8 bytes
    _Atomic(lz_chunk_header_t*) current_chunk; // 8 bytes
    lz_chunk_header_t* head_chunk; // 8 bytes

    // --- CACHE LINE 2: Global Metadata and Slow-Path ---
    _Atomic(lz_huge_node_t*) huge_list_head; // 8 bytes
    lz_stat_slot_t* stats_slot;              // 8 bytes [Phase 5.3 - SHM Pointer]

    // Size Grouping (24 bytes)
    _Atomic(size_t) total_allocated_bytes;
    size_t telemetry_requested; // [Phase 5.3] Double-entry accounting
    size_t telemetry_allocated; // [Phase 5.3] Double-entry accounting

    // Synchronization primitives (At the end to prevent pointer misalignment)
    atomic_flag expansion_lock;    
} lz_region_t;

/* ========================================================================= *
 * Region Engine Public API
 * ========================================================================= */

/**
 * @brief Creates a new semantic memory region.
 * @return Pointer to the region, or NULL if the system is OOM.
 */
lz_region_t* lz_region_create(void);

/**
 * @brief Destroys an entire region, returning all its Chunks to the VMM.
 * @note Destroying a region instantly invalidates ALL pointers allocated within it.
 * @param region Pointer to the region to destroy.
 */
void lz_region_destroy(lz_region_t* region);

/* ========================================================================= *
 * Internal Declarations (Slow Path)
 * ========================================================================= */

/**
 * @brief Slow path: Expands the region or handles aligned huge objects.
 * @param region Target region.
 * @param alignment Desired alignment (power of 2).
 * @param size Requested size (already adjusted to minimum 8 bytes).
 * @return Pointer to allocated memory.
 */
void* lz_region_alloc_slow(lz_region_t* region, size_t alignment, size_t size);

/* ========================================================================= *
 * Concurrent Fast-Path (Pure O(1) Allocation)
 * ========================================================================= */

/**
 * @brief Allocates sequential memory with specific alignment. Thread-safe lock-free.
 * @param region Target region.
 * @param alignment Desired alignment in bytes (must be power of 2).
 * @param size Requested size in bytes.
 * @return Pointer to the allocated and aligned memory.
 */
static LZ_ALWAYS_INLINE void* lz_region_alloc_aligned(lz_region_t* region, size_t alignment, size_t size) {
    if (LZ_UNLIKELY(size == 0)) return NULL;
    if (LZ_UNLIKELY(alignment == 0 || (alignment & (alignment - 1)) != 0)) return NULL;

    // Force a minimum of 8 bytes alignment for architectural safety
    if (alignment < 8) alignment = 8;
    
    size_t padded_size = LZ_ALIGN_UP(size, 8);

    char* current_ptr = atomic_load_explicit(&region->bump_ptr, memory_order_relaxed);
    char* aligned_start;
    char* new_bump;

    // Lock-Free CAS Loop with Dynamic Alignment Calculation
    do {
        aligned_start = (char*)LZ_ALIGN_UP((uintptr_t)current_ptr, alignment);
        new_bump = aligned_start + padded_size;

        if (LZ_UNLIKELY(new_bump > region->chunk_end)) {
            // Cannot fit even with alignment padding. Fallback to slow path.
            return lz_region_alloc_slow(region, alignment, padded_size);
        }

    } while (!atomic_compare_exchange_weak_explicit(
                &region->bump_ptr, 
                &current_ptr, 
                new_bump, 
                memory_order_relaxed, 
                memory_order_relaxed));

    // Telemetry and Accounting
    size_t consumed_bytes = (size_t)(new_bump - current_ptr);
    atomic_fetch_add_explicit(&region->total_allocated_bytes, consumed_bytes, memory_order_relaxed);
    
    region->telemetry_requested += size; 
    if (LZ_LIKELY(region->stats_slot)) {
        atomic_fetch_add_explicit(&region->stats_slot->bytes_requested, size, memory_order_relaxed);
    }

    return (void*)aligned_start;
}

/**
 * @brief Allocates sequential memory (Default 8-byte alignment).
 * @param region Target region.
 * @param size Requested size in bytes.
 * @return Pointer to the allocated memory.
 */
static LZ_ALWAYS_INLINE void* lz_region_alloc(lz_region_t* region, size_t size) {
    return lz_region_alloc_aligned(region, 8, size);
}

#endif // LZ_REGION_H