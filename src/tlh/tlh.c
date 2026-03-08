/**
 * @file tlh.c
 * @brief Implementation of the Thread-Local Heap allocation engine.
 */

#include "tlh.h"
#include "slab.h"
#include "span.h"
#include "chunk.h"
#include "rtree.h"
#include "security.h"
#include <stddef.h>

/* ========================================================================= *
 * Internal Slab Lifecycle Management
 * ========================================================================= */

static void lz_tlh_rescue_from_full(lz_tlh_t* tlh, lz_slab_t* slab) {
    lz_bin_t* bin = &tlh->bins[slab->size_class];

    if (LZ_UNLIKELY(slab == bin->current_slab)) {
        return;
    }

    if (slab->prev) slab->prev->next = slab->next;
    else bin->full_list = slab->next;
    
    if (slab->next) slab->next->prev = slab->prev;

    slab->next = bin->partial_list;
    slab->prev = NULL;
    
    if (bin->partial_list) bin->partial_list->prev = slab;
    bin->partial_list = slab;
}

static void lz_tlh_recycle_empty_slab(lz_tlh_t* tlh, lz_slab_t* slab) {
    lz_bin_t* bin = &tlh->bins[slab->size_class];

    if (LZ_UNLIKELY(slab == bin->current_slab)) {
        if (bin->partial_list == NULL) return; /* Anti-chattering hysteresis */
        
        bin->current_slab = bin->partial_list;
        bin->partial_list = bin->partial_list->next;
        if (bin->partial_list) bin->partial_list->prev = NULL;
        
        bin->current_slab->next = NULL;
        bin->current_slab->prev = NULL;
    } 
    else {
        if (slab->prev) slab->prev->next = slab->next;
        else {
            if (bin->partial_list == slab) bin->partial_list = slab->next;
            else if (bin->full_list == slab) bin->full_list = slab->next;
        }
        if (slab->next) slab->next->prev = slab->prev;
    }

    lz_slab_destroy(slab);
}

/* ========================================================================= *
 * Batching and Flushing (Remote Frees)
 * ========================================================================= */

void lz_tlh_flush_outgoing_batch(lz_tlh_t* tlh) {
    lz_batch_t* batch = &tlh->outgoing_batch;
    if (LZ_UNLIKELY(batch->count == 0 || !batch->target_tlh)) {
        return;
    }

    lz_tlh_t* target = batch->target_tlh;
    lz_free_node_t* old_head = atomic_load_explicit(&target->remote_free_head, memory_order_relaxed);
    
    /* Lock-free Compare-And-Swap loop to push the entire batch in O(1) */
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
    /* Extract the entire linked list of remote frees in a single atomic sweep */
    lz_free_node_t* head = atomic_exchange_explicit(&tlh->remote_free_head, NULL, memory_order_acquire);
    if (LZ_LIKELY(!head)) {
        return;
    }

    uintptr_t chunk_mask = ~(((uintptr_t)1 << LZ_CHUNK_SHIFT) - 1);
    
    while (head) {
        lz_free_node_t* next_clear = head->next;
        lz_chunk_header_t* chunk = (lz_chunk_header_t*)((uintptr_t)head & chunk_mask);
        
        /* ROUTING: Discriminate payload type and route to the correct engine */
        if (LZ_LIKELY(chunk->chunk_type == LZ_CHUNK_TYPE_SLAB)) {
            lz_slab_t* slab = (lz_slab_t*)((char*)chunk + LZ_CACHE_LINE_SIZE);
            
            void* current_free_head = slab->free_list_head;
            head->next = (lz_free_node_t*)lz_ptr_obfuscate(current_free_head, &head->next);
            slab->free_list_head = head;
            
            slab->used_objects--;
            
            if (LZ_UNLIKELY(slab->used_objects == 0)) {
                lz_tlh_recycle_empty_slab(tlh, slab);
            }
        } 
        else if (chunk->chunk_type == LZ_CHUNK_TYPE_SPAN) {
            lz_span_free_remote(tlh, chunk, (void*)head);
        }

        head = next_clear;
    }
}

/* ========================================================================= *
 * Main API: Alloc Helpers (Cold Path Extraction)
 * ========================================================================= */

/**
 * @brief Extracted slow-path for Slab allocation. 
 * Minimizes the instruction footprint of the fast-path in the CPU L1i cache.
 */
static __attribute__((noinline)) void* lz_tlh_alloc_slow(lz_tlh_t* tlh, lz_bin_t* bin, uint32_t sc_idx) {
    lz_slab_t* slab = bin->current_slab;
    void* ptr = NULL;

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
    }

    return ptr;
}

/* ========================================================================= *
 * Main API: Alloc & Free
 * ========================================================================= */

void lz_tlh_init(lz_tlh_t* tlh, uint32_t tid) {
    tlh->thread_id = tid;
    tlh->is_zombie = 0;
    tlh->bytes_allocated = 0;
    
    tlh->local_bytes_alloc_batch = 0;
    tlh->local_bytes_free_batch = 0;
    tlh->stat_slot = NULL; 
    tlh->active_spans = NULL;

    atomic_init(&tlh->remote_free_head, NULL);

    tlh->outgoing_batch.target_tlh = NULL;
    tlh->outgoing_batch.head = NULL;
    tlh->outgoing_batch.tail = NULL;
    tlh->outgoing_batch.count = 0;
    tlh->outgoing_batch._padding = 0;

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

    /* 1. FASTEST PATH: Obfuscated Free List */
    if (LZ_LIKELY(slab && slab->free_list_head)) {
        lz_free_node_t* node = (lz_free_node_t*)slab->free_list_head;
        void* obfuscated_next = node->next;
        slab->free_list_head = lz_ptr_obfuscate(obfuscated_next, &node->next);
        slab->used_objects++;
        ptr = (void*)node;
        allocated_size = slab->block_size;
    }
    /* 2. WARM PATH: Reap Remote Frees & Bump Allocation */
    else {
        lz_tlh_reap(tlh);
        if (slab && slab->free_list_head) {
            lz_free_node_t* node = (lz_free_node_t*)slab->free_list_head;
            slab->free_list_head = lz_ptr_obfuscate(node->next, &node->next);
            slab->used_objects++;
            ptr = (void*)node;
            allocated_size = slab->block_size;
        }
        else if (LZ_LIKELY(slab && slab->bump_ptr != NULL)) {
            ptr = slab->bump_ptr;
            void* next_bump = (char*)ptr + slab->block_size;
            
            if (next_bump <= slab->memory_end) {
                slab->bump_ptr = next_bump;
                slab->used_objects++;
                allocated_size = slab->block_size;
            } else {
                slab->bump_ptr = NULL; 
                ptr = NULL; 
            }
        }
    }

    /* 3. SLOW PATH: Partial list promotion or VMM Chunk allocation */
    if (LZ_UNLIKELY(!ptr)) {
        ptr = lz_tlh_alloc_slow(tlh, bin, sc_idx);
        if (LZ_UNLIKELY(!ptr)) return NULL;
        allocated_size = bin->current_slab->block_size;
    }

    /* Telemetry Batching */
    tlh->bytes_allocated += allocated_size;
    if (LZ_UNLIKELY(tlh->stat_slot)) {
        tlh->local_bytes_alloc_batch += allocated_size;
        if (LZ_UNLIKELY(tlh->local_bytes_alloc_batch >= 1048576)) {
            atomic_fetch_add_explicit(&tlh->stat_slot->bytes_allocated, tlh->local_bytes_alloc_batch, memory_order_relaxed);
            tlh->local_bytes_alloc_batch = 0;
        }
    }

    return ptr;
}

void lz_tlh_free(lz_tlh_t* tlh, void* ptr) {
    if (LZ_UNLIKELY(!ptr)) return;

    uintptr_t chunk_mask = ~(((uintptr_t)1 << LZ_CHUNK_SHIFT) - 1);
    lz_chunk_header_t* chunk = (lz_chunk_header_t*)((uintptr_t)ptr & chunk_mask);

    /* Radix Tree fallback to prevent false chunk starts on maliciously offset pointers */
    if (LZ_UNLIKELY(chunk->magic != LZ_CHUNK_MAGIC_V2)) {
        chunk = lz_rtree_get(ptr); 
        if (!chunk) return; 
    }

    /* 1. LOCAL FREE */
    if (LZ_LIKELY(chunk->owning_tlh == tlh)) {
        /* ROUTING: Route to corresponding internal engine */
        if (LZ_LIKELY(chunk->chunk_type == LZ_CHUNK_TYPE_SLAB)) {
            lz_slab_t* slab = (lz_slab_t*)((char*)chunk + LZ_CACHE_LINE_SIZE);
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

            /* Fast telemetry batching for Slabs */
            if (LZ_UNLIKELY(tlh->stat_slot)) {
                tlh->local_bytes_free_batch += slab->block_size;
                if (LZ_UNLIKELY(tlh->local_bytes_free_batch >= 4096)) {
                    atomic_fetch_sub_explicit(&tlh->stat_slot->bytes_allocated, tlh->local_bytes_free_batch, memory_order_relaxed);
                    tlh->local_bytes_free_batch = 0;
                }
            }
        } 
        else if (chunk->chunk_type == LZ_CHUNK_TYPE_SPAN) {
            lz_span_free_local(tlh, chunk, ptr);
        }
    }
    /* 2. REMOTE FREE (Unified Batching for all engines) */
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
}