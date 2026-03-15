/**
 * @file core_heap.c
 * @brief Implementación del array global de Heaps y la recolección asíncrona (MPSC).
 */
#include "core_heap.h"
#include "atomics.h"
#include "security.h"
#include "pagemap.h"
#include "lz_log.h"

extern void lz_slab_free_local(lz_core_heap_t* heap, lz_chunk_t* chunk, void* ptr);
extern void lz_span_free_local(lz_core_heap_t* heap, lz_chunk_t* chunk, void* ptr);

/* Array mapeado estáticamente en el binario (.bss). */
lz_core_heap_t g_core_heaps[LZ_MAX_CORES] LZ_CACHELINE_ALIGNED;

void lz_core_heap_remote_free(uint32_t target_core_id, void* ptr) {
    lz_core_heap_t* target_heap = &g_core_heaps[target_core_id];
    lz_free_node_t* node = (lz_free_node_t*)ptr;

    /* Push Atómico Lock-Free. 
     * NOTA: No usamos ABA tagging aquí. El ABA es matemáticamente imposible 
     * en esta cola porque el consumidor extrae todos los elementos con XCHG atómico. */
    uint64_t current_mailbox = lz_atomic_load_acquire(&target_heap->remote_free_mailbox);
    
    do {
        node->next = (lz_free_node_t*)lz_ptr_obfuscate((void*)current_mailbox, (void**)&node->next);
    } while (!lz_atomic_cas_weak(&target_heap->remote_free_mailbox, &current_mailbox, (uint64_t)node));
}

void lz_core_heap_reap_mailbox(lz_core_heap_t* heap) {
    /* Robo atómico ultra-rápido: Vaciamos el buzón completo en 1 ciclo de reloj */
    uint64_t mailbox_state = lz_atomic_exchange(&heap->remote_free_mailbox, 0);
    
    lz_free_node_t* current = (lz_free_node_t*)mailbox_state;
    if (LZ_LIKELY(!current)) return;

    /* Procesamiento Local ininterrumpido */
    while (current) {
        lz_free_node_t* next_node = (lz_free_node_t*)lz_ptr_obfuscate(current->next, (void**)&current->next);
        lz_chunk_t* chunk = lz_meta_resolve(current);

        if (LZ_LIKELY(chunk)) {
            if (chunk->chunk_type == LZ_CHUNK_TYPE_SLAB) {
                lz_slab_free_local(heap, chunk, current);
            } else if (chunk->chunk_type == LZ_CHUNK_TYPE_SPAN) {
                lz_span_free_local(heap, chunk, current);
            }
        } else {
            LZ_ERROR("Core Heap: Se intentó procesar un Remote Free con un Chunk inválido (%p).", current);
        }

        current = next_node;
    }
}