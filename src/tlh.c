/**
 * @file tlh.c
 * @brief Implementation of the Thread-Local Heap allocation engine.
 */

#include "tlh.h"
#include "slab.h"
#include "chunk.h"
#include "rtree.h"
#include "security.h"
#include <stddef.h>

/* ========================================================================= *
 * Internal Slab Lifecycle Management
 * ========================================================================= */

/**
 * @brief Rescues a Slab from the full_list and moves it to the partial_list.
 * Triggered exactly when a full Slab receives its first free().
 */
static void lz_tlh_rescue_from_full(lz_tlh_t* tlh, lz_slab_t* slab) {
    lz_bin_t* bin = &tlh->bins[slab->size_class];

    // Structural safety: if it's the current_slab, do nothing.
    if (LZ_UNLIKELY(slab == bin->current_slab)) return;

    // 1. Unlink from full_list
    if (slab->prev) {
        slab->prev->next = slab->next;
    } else {
        bin->full_list = slab->next;
    }
    if (slab->next) {
        slab->next->prev = slab->prev;
    }

    // 2. Link to the head of partial_list
    slab->next = bin->partial_list;
    slab->prev = NULL;
    if (bin->partial_list) {
        bin->partial_list->prev = slab;
    }
    bin->partial_list = slab;
}

/**
 * @brief Unlinks and recycles an empty Slab, returning it to the VMM.
 * Executes O(1) doubly-linked list operations.
 */
static void lz_tlh_recycle_empty_slab(lz_tlh_t* tlh, lz_slab_t* slab) {
    lz_bin_t* bin = &tlh->bins[slab->size_class];

    // Case 1: It is the active Slab (current_slab)
    if (LZ_UNLIKELY(slab == bin->current_slab)) {
        // Anti-chattering: keep it if we don't have partial slabs in reserve.
        if (bin->partial_list == NULL) {
            return; 
        }
        
        // Promote the first partial Slab to current_slab
        bin->current_slab = bin->partial_list;
        bin->partial_list = bin->partial_list->next;
        if (bin->partial_list) {
            bin->partial_list->prev = NULL;
        }
        
        // Isolate the new current_slab
        bin->current_slab->next = NULL;
        bin->current_slab->prev = NULL;
    } 
    // Case 2: It is in the partial_list or full_list
    else {
        if (slab->prev) {
            slab->prev->next = slab->next;
        } else {
            if (bin->partial_list == slab) {
                bin->partial_list = slab->next;
            } else if (bin->full_list == slab) {
                bin->full_list = slab->next;
            }
        }
        
        if (slab->next) {
            slab->next->prev = slab->prev;
        }
    }

    lz_slab_destroy(slab);
}

/* ========================================================================= *
 * Batching and Flushing (Remote Frees)
 * ========================================================================= */

void lz_tlh_flush_outgoing_batch(lz_tlh_t* tlh) {
    lz_batch_t* batch = &tlh->outgoing_batch;
    if (LZ_UNLIKELY(batch->count == 0 || !batch->target_tlh)) return;

    lz_tlh_t* target = batch->target_tlh;
    
    lz_free_node_t* old_head = atomic_load_explicit(&target->remote_free_head, memory_order_relaxed);
    do {
        batch->tail->next = old_head; 
    } while (!atomic_compare_exchange_weak_explicit(&target->remote_free_head, 
                                                    &old_head, batch->head,
                                                    memory_order_release, 
                                                    memory_order_relaxed));

    batch->target_tlh = NULL;
    batch->head = NULL;
    batch->tail = NULL;
    batch->count = 0;
}

void lz_tlh_reap(lz_tlh_t* tlh) {
    lz_free_node_t* head = atomic_exchange_explicit(&tlh->remote_free_head, NULL, memory_order_acquire);
    uintptr_t chunk_mask = ~(((uintptr_t)1 << LZ_CHUNK_SHIFT) - 1);
    
    while (head) {
        lz_free_node_t* next_clear = head->next;
        
        lz_chunk_header_t* chunk = (lz_chunk_header_t*)((uintptr_t)head & chunk_mask);
        lz_slab_t* slab = (lz_slab_t*)((char*)chunk + LZ_CACHE_LINE_SIZE);
        
        void* current_free_head = slab->free_list_head;
        head->next = (lz_free_node_t*)lz_ptr_obfuscate(current_free_head, &head->next);
        slab->free_list_head = head;
        
        slab->used_objects--;
        
        if (LZ_UNLIKELY(slab->used_objects == 0)) {
            lz_tlh_recycle_empty_slab(tlh, slab);
        }

        head = next_clear;
    }
}

/* ========================================================================= *
 * Main API (Alloc / Free)
 * ========================================================================= */

void lz_tlh_init(lz_tlh_t* tlh, uint32_t tid) {
    tlh->thread_id = tid;
    tlh->is_zombie = 0;
    tlh->bytes_allocated = 0;
    
    tlh->local_bytes_alloc_batch = 0;
    tlh->local_bytes_free_batch = 0;
    tlh->stat_slot = NULL; 

    atomic_init(&tlh->remote_free_head, NULL);

    tlh->outgoing_batch.target_tlh = NULL;
    tlh->outgoing_batch.head = NULL;
    tlh->outgoing_batch.tail = NULL;
    tlh->outgoing_batch.count = 0;

    for (int i = 0; i < LZ_MAX_SIZE_CLASSES; ++i) {
        tlh->bins[i].current_slab = NULL;
        tlh->bins[i].partial_list = NULL;
        tlh->bins[i].full_list = NULL;
    }
}

void* lz_tlh_alloc(lz_tlh_t* tlh, size_t size) {
    uint32_t sc_idx = lz_size_to_class(size);
    lz_bin_t* bin = &tlh->bins[sc_idx];
    lz_slab_t* slab = bin->current_slab;
    void* ptr = NULL;
    size_t allocated_size = 0; 

    // 1. FASTEST PATH
    if (LZ_LIKELY(slab && slab->free_list_head)) {
        lz_free_node_t* node = (lz_free_node_t*)slab->free_list_head;
        void* obfuscated_next = node->next;
        slab->free_list_head = lz_ptr_obfuscate(obfuscated_next, &node->next);
        slab->used_objects++;
        ptr = (void*)node;
        allocated_size = slab->block_size;
    }
    // 2. WARM PATH
    else {
        lz_tlh_reap(tlh);
        if (slab && slab->free_list_head) {
            lz_free_node_t* node = (lz_free_node_t*)slab->free_list_head;
            slab->free_list_head = lz_ptr_obfuscate(node->next, &node->next);
            slab->used_objects++;
            ptr = (void*)node;
            allocated_size = slab->block_size;
        }
        // 3. BUMP ALLOCATION PATH
        else if (LZ_LIKELY(slab && slab->bump_ptr != NULL)) {
            ptr = slab->bump_ptr;
            void* next_bump = (char*)ptr + slab->block_size;
            
            if (next_bump <= slab->memory_end) {
                slab->bump_ptr = next_bump;
                slab->used_objects++;
                allocated_size = slab->block_size;
            } else {
                slab->bump_ptr = NULL; 
                ptr = NULL; // Fallback to PRE-SLOW PATH
            }
        }
    }

    // 4 & 5. PRE-SLOW & SLOW PATH
    if (LZ_UNLIKELY(!ptr)) {
        if (bin->partial_list != NULL) {
            if (slab != NULL) {
                slab->next = bin->full_list;
                slab->prev = NULL;
                if (bin->full_list) bin->full_list->prev = slab;
                bin->full_list = slab;
            }

            slab = bin->partial_list;
            bin->current_slab = slab;
            bin->partial_list = slab->next;
            if (bin->partial_list) bin->partial_list->prev = NULL;
            
            slab->next = NULL;
            slab->prev = NULL;

            lz_free_node_t* node = (lz_free_node_t*)slab->free_list_head;
            slab->free_list_head = lz_ptr_obfuscate(node->next, &node->next);
            slab->used_objects++;
            ptr = (void*)node;
            allocated_size = slab->block_size;
        } else {
            lz_slab_t* new_slab = lz_slab_create(tlh, sc_idx);
            if (LZ_UNLIKELY(!new_slab)) return NULL;

            if (slab != NULL) {
                slab->next = bin->full_list;
                slab->prev = NULL;
                if (bin->full_list) bin->full_list->prev = slab;
                bin->full_list = slab;
            }

            bin->current_slab = new_slab;
            ptr = new_slab->bump_ptr;
            new_slab->bump_ptr = (char*)ptr + new_slab->block_size;
            new_slab->used_objects++;
            allocated_size = new_slab->block_size;
        }
    }

    // Telemetry Batching
    if (LZ_LIKELY(ptr)) {
        tlh->bytes_allocated += allocated_size;
        
        if (LZ_UNLIKELY(tlh->stat_slot)) {
            tlh->local_bytes_alloc_batch += allocated_size;
            if (LZ_UNLIKELY(tlh->local_bytes_alloc_batch >= 4096)) {
                atomic_fetch_add_explicit(&tlh->stat_slot->bytes_allocated, tlh->local_bytes_alloc_batch, memory_order_relaxed);
                tlh->local_bytes_alloc_batch = 0;
            }
        }
    }

    return ptr;
}

void lz_tlh_free(lz_tlh_t* tlh, void* ptr) {
    if (LZ_UNLIKELY(!ptr)) return;

    uintptr_t chunk_mask = ~(((uintptr_t)1 << LZ_CHUNK_SHIFT) - 1);
    lz_chunk_header_t* chunk = (lz_chunk_header_t*)((uintptr_t)ptr & chunk_mask);

    // Fallback to Radix Tree if magic doesn't match (prevents false chunk starts)
    if (LZ_UNLIKELY(chunk->magic != LZ_CHUNK_MAGIC_V2)) {
        chunk = lz_rtree_get(ptr); 
        if (!chunk) return; 
    }

    if (LZ_UNLIKELY(chunk->is_lsm_region)) return;

    lz_slab_t* slab = (lz_slab_t*)((char*)chunk + LZ_CACHE_LINE_SIZE);
    size_t freed_size = slab->block_size; 

    // 1. LOCAL FREE
    if (LZ_LIKELY(chunk->owning_tlh == tlh)) {
        lz_free_node_t* node = (lz_free_node_t*)ptr;
        void* clear_next = slab->free_list_head;
        node->next = (lz_free_node_t*)lz_ptr_obfuscate(clear_next, &node->next);
        
        slab->free_list_head = node;
        slab->used_objects--;

        if (LZ_UNLIKELY(slab->used_objects == slab->max_objects - 1)) {
            lz_tlh_rescue_from_full(tlh, slab);
        }

        if (LZ_UNLIKELY(slab->used_objects == 0)) {
            lz_tlh_recycle_empty_slab(tlh, slab);
        }
    }
    // 2. REMOTE FREE
    else {
        lz_tlh_t* owner = (lz_tlh_t*)chunk->owning_tlh;
        lz_batch_t* batch = &tlh->outgoing_batch;
        lz_free_node_t* node = (lz_free_node_t*)ptr;

        node->next = NULL; 

        if (batch->target_tlh != owner || batch->count >= LZ_BATCH_MAX_SIZE) {
            lz_tlh_flush_outgoing_batch(tlh);
            batch->target_tlh = owner;
        }

        node->next = batch->head;
        batch->head = node;
        if (batch->count == 0) batch->tail = node;
        batch->count++;
    }

    // Telemetry Batching
    if (LZ_UNLIKELY(tlh->stat_slot)) {
        tlh->local_bytes_free_batch += freed_size;
        if (LZ_UNLIKELY(tlh->local_bytes_free_batch >= 4096)) {
            atomic_fetch_sub_explicit(&tlh->stat_slot->bytes_allocated, tlh->local_bytes_free_batch, memory_order_relaxed);
            tlh->local_bytes_free_batch = 0;
        }
    }
}