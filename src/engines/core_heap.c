/**
 * @file core_heap.c
 * @brief Core Heap Orchestration and MPSC Mailbox Implementation.
 * @details Manages the lifecycle of per-core arenas and implements 
 * wait-free remote deallocation via atomic mailbox exchanges.
 */
#include "core_heap.h"
#include "atomics.h"
#include "security.h"
#include "pagemap.h"
#include "lz_log.h"

/* Forward declarations for local engine dispatchers */
extern void lz_slab_free_local(lz_core_heap_t* heap, lz_chunk_t* chunk, void* ptr);
extern void lz_span_free_local(lz_core_heap_t* heap, lz_chunk_t* chunk, void* ptr);

/** @internal Statically mapped global heaps. */
lz_core_heap_t g_core_heaps[LZ_TOTAL_HEAPS] LZ_CACHELINE_ALIGNED;

void lz_core_heap_remote_free(uint32_t target_core_id, void* ptr) {
    lz_core_heap_t* target_heap = &g_core_heaps[target_core_id];
    
    /* Sanitization Injection: Ensure lazy-init cores are identified */
    if (LZ_UNLIKELY(!target_heap->is_initialized)) {
        target_heap->core_id = target_core_id;
        target_heap->is_initialized = 1;
    }

    lz_free_node_t* node = (lz_free_node_t*)ptr;
    /* Atomic Acquire to synchronize with the current mailbox state */
    uint64_t current_mailbox = lz_atomic_load_acquire(&target_heap->remote_free_mailbox);
    
    do {
        /* Safe Linking: Mask the next pointer with the current mailbox value and secret */
        node->next = (lz_free_node_t*)lz_ptr_obfuscate((void*)current_mailbox, (void**)&node->next);
        /* Atomic CAS-weak loop for wait-free ingestion */
    } while (!lz_atomic_cas_weak(&target_heap->remote_free_mailbox, &current_mailbox, (uint64_t)node));
}

void lz_core_heap_reap_mailbox(lz_core_heap_t* heap) {
    /**
     * @brief Atomic Robbery: Drain the entire mailbox in a single cycle.
     * Uses Acquire/Release semantics to synchronize cross-thread deallocations.
     */
    uint64_t mailbox_state = lz_atomic_exchange(&heap->remote_free_mailbox, 0);
    
    lz_free_node_t* current = (lz_free_node_t*)mailbox_state;
    if (LZ_LIKELY(!current)) return;

    /* Process the linked list locally without further contention */
    while (current) {
        /* De-obfuscate the pointer to retrieve the next node */
        lz_free_node_t* next_node = (lz_free_node_t*)lz_ptr_obfuscate(current->next, (void**)&current->next);
        
        /* Resolve metadata header in O(1) */
        lz_chunk_t* chunk = lz_meta_resolve(current);

        if (LZ_LIKELY(chunk)) {
            /* Route to the appropriate local engine based on chunk type */
            if (chunk->chunk_type == LZ_CHUNK_TYPE_SLAB) {
                lz_slab_free_local(heap, chunk, current);
            } else if (chunk->chunk_type == LZ_CHUNK_TYPE_SPAN) {
                lz_span_free_local(heap, chunk, current);
            }
        } else {
            LZ_ERROR("Core Heap: Invalid Chunk resolve during remote free processing (%p).", current);
        }

        current = next_node;
    }
}