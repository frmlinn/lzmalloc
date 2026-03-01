/**
 * @file region.c
 * @brief Implementation of the Arena Allocator lifecycle.
 */

#define _GNU_SOURCE 
#include "region.h"
#include "vmm.h"
#include "rtree.h"
#include "telemetry.h"
#include <sys/mman.h>
#include <stddef.h>

/* ========================================================================= *
 * Forward Declarations
 * ========================================================================= */

static void* lz_region_alloc_huge(lz_region_t* region, size_t size);

/* ========================================================================= *
 * Region Expansion (Slow-Path)
 * ========================================================================= */

void* lz_region_alloc_slow(lz_region_t* region, size_t alignment, size_t size) {
    size_t padded_size = LZ_ALIGN_UP(size, 8); 
    size_t max_payload = LZ_HUGE_PAGE_SIZE - LZ_CACHE_LINE_SIZE;
    
    // Fallback to Huge Allocator if payload exceeds a single Chunk
    if (LZ_UNLIKELY(padded_size + alignment > max_payload)) {
        return lz_region_alloc_huge(region, size); 
    }

    // Acquire Expansion Spinlock
    while (atomic_flag_test_and_set_explicit(&region->expansion_lock, memory_order_acquire)) {
        lz_cpu_relax(); 
    }

    // Double-Checked Locking with Alignment
    char* current_ptr = atomic_load_explicit(&region->bump_ptr, memory_order_relaxed);
    char* check_aligned_start = (char*)LZ_ALIGN_UP((uintptr_t)current_ptr, alignment);
    char* check_new_bump = check_aligned_start + padded_size;

    if (LZ_LIKELY(check_new_bump <= region->chunk_end)) {
        // Another thread expanded the region while we were waiting!
        atomic_flag_clear_explicit(&region->expansion_lock, memory_order_release);
        return lz_region_alloc_aligned(region, alignment, size);
    }

    // Request new Chunk from the VMM
    lz_chunk_header_t* new_chunk = lz_vmm_alloc_chunk();
    if (LZ_UNLIKELY(!new_chunk)) {
        atomic_flag_clear_explicit(&region->expansion_lock, memory_order_release);
        return NULL; // Critical OOM
    }

    // Shielding and Routing for the new Chunk
    new_chunk->is_lsm_region = 1;
    new_chunk->owning_tlh = NULL;
    new_chunk->checksum = lz_calc_checksum(new_chunk);
    lz_rtree_set((uintptr_t)new_chunk, new_chunk);

    // Chain the new Chunk
    lz_chunk_header_t* old_current = atomic_load_explicit(&region->current_chunk, memory_order_relaxed);
    old_current->next = new_chunk;
    atomic_store_explicit(&region->current_chunk, new_chunk, memory_order_relaxed);

    // Calculate limits and apply ALIGNMENT on the new Chunk
    char* raw_data_start = (char*)new_chunk + LZ_CACHE_LINE_SIZE;
    char* data_end = (char*)new_chunk + LZ_HUGE_PAGE_SIZE;

    char* aligned_start = (char*)LZ_ALIGN_UP((uintptr_t)raw_data_start, alignment);
    char* new_bump = aligned_start + padded_size;

    // Publish new boundaries to other threads
    region->chunk_end = data_end;
    atomic_store_explicit(&region->bump_ptr, new_bump, memory_order_release);
    
    size_t consumed_bytes = (size_t)(new_bump - raw_data_start);
    atomic_fetch_add_explicit(&region->total_allocated_bytes, consumed_bytes, memory_order_relaxed);

    // Strict Double-Entry Telemetry
    region->telemetry_requested += size;
    region->telemetry_allocated += LZ_HUGE_PAGE_SIZE; 

    if (LZ_LIKELY(region->stats_slot)) {
        atomic_fetch_add_explicit(&region->stats_slot->bytes_requested, size, memory_order_relaxed);
        atomic_fetch_add_explicit(&region->stats_slot->bytes_allocated, LZ_HUGE_PAGE_SIZE, memory_order_relaxed);
        atomic_fetch_add_explicit(&region->stats_slot->events, 1, memory_order_relaxed); 
    }

    // Release Spinlock
    atomic_flag_clear_explicit(&region->expansion_lock, memory_order_release);

    return (void*)aligned_start;
}

/* ========================================================================= *
 * Huge Object Management
 * ========================================================================= */

static void* lz_region_alloc_huge(lz_region_t* region, size_t size) {
    size_t total_size = size + sizeof(lz_huge_node_t);
    total_size = LZ_ALIGN_UP(total_size, LZ_PAGE_SIZE); // Platform-agnostic OS page alignment

    lz_huge_node_t* node = (lz_huge_node_t*)mmap(NULL, total_size, 
                                                 PROT_READ | PROT_WRITE, 
                                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                                                 
    if (LZ_UNLIKELY(node == MAP_FAILED)) return NULL;

    node->size = total_size;

    // Lock-free injection into the region's huge object list
    lz_huge_node_t* old_head = atomic_load_explicit(&region->huge_list_head, memory_order_relaxed);
    do {
        node->next = old_head;
    } while (!atomic_compare_exchange_weak_explicit(&region->huge_list_head, 
                                                    &old_head, node, 
                                                    memory_order_release, 
                                                    memory_order_relaxed));

    atomic_fetch_add_explicit(&region->total_allocated_bytes, size, memory_order_relaxed);

    return (void*)((char*)node + sizeof(lz_huge_node_t));
}

/* ========================================================================= *
 * Region Creation
 * ========================================================================= */

lz_region_t* lz_region_create(void) {
    lz_chunk_header_t* chunk = lz_vmm_alloc_chunk();
    if (LZ_UNLIKELY(!chunk)) return NULL;

    chunk->is_lsm_region = 1;
    chunk->owning_tlh = NULL; 
    chunk->checksum = lz_calc_checksum(chunk);
    lz_rtree_set((uintptr_t)chunk, chunk);

    lz_region_t* region = (lz_region_t*)((char*)chunk + LZ_CACHE_LINE_SIZE);

    region->head_chunk = chunk;
    atomic_init(&region->current_chunk, chunk);
    atomic_flag_clear(&region->expansion_lock);
    atomic_init(&region->huge_list_head, NULL); 
    atomic_init(&region->total_allocated_bytes, 0);

    char* data_start = (char*)region + sizeof(lz_region_t);
    data_start = (char*)LZ_ALIGN_UP((uintptr_t)data_start, 8);
    char* data_end = (char*)chunk + LZ_HUGE_PAGE_SIZE;

    atomic_init(&region->bump_ptr, data_start);
    region->chunk_end = data_end;

    region->telemetry_requested = 0;
    region->telemetry_allocated = LZ_HUGE_PAGE_SIZE; 

    // Sharding: Assign a fixed telemetry slot based on the region's address
    uintptr_t hash = ((uintptr_t)region >> 12) % LZ_LSM_SLOTS_COUNT;
    region->stats_slot = lz_telemetry_get_slot(LZ_LSM_SLOT_BASE_IDX + hash);

    if (LZ_LIKELY(region->stats_slot)) {
        atomic_fetch_add_explicit(&region->stats_slot->active_objects, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&region->stats_slot->bytes_allocated, LZ_HUGE_PAGE_SIZE, memory_order_relaxed);
    }

    return region;
}

/* ========================================================================= *
 * Region Destruction (The Mass Reaper)
 * ========================================================================= */

void lz_region_destroy(lz_region_t* region) {
    if (LZ_UNLIKELY(!region)) return;

    if (LZ_LIKELY(region->stats_slot)) {
        atomic_fetch_sub_explicit(&region->stats_slot->active_objects, 1, memory_order_relaxed);
        atomic_fetch_sub_explicit(&region->stats_slot->bytes_requested, region->telemetry_requested, memory_order_relaxed);
        atomic_fetch_sub_explicit(&region->stats_slot->bytes_allocated, region->telemetry_allocated, memory_order_relaxed);
    }

    // 1. Unmap all direct-mapped huge objects
    lz_huge_node_t* huge = atomic_load_explicit(&region->huge_list_head, memory_order_acquire);
    while (huge != NULL) {
        lz_huge_node_t* next_huge = huge->next;
        munmap(huge, huge->size); 
        huge = next_huge;
    }

    // 2. Begin mass recycling of Chunks
    lz_chunk_header_t* current = region->head_chunk;
    while (current != NULL) {
        if (LZ_LIKELY(current->checksum == lz_calc_checksum(current))) {
            lz_chunk_header_t* next_chunk = current->next;
            
            lz_rtree_clear((uintptr_t)current);
            lz_vmm_free_chunk(current);
            
            current = next_chunk;
        } else {
            // Critical corruption detected in the Chunk chain.
            break; 
        }
    }
}