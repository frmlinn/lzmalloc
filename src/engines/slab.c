/**
 * @file slab.c
 * @brief Implementación completa del Slab Engine (Bump-Pointer y Free-List).
 */
#include "slab.h"
#include "vmm.h"
#include "pagemap.h"
#include "security.h"
#include "lz_log.h"
#include <stddef.h>
#include <stdint.h>

void* lz_slab_alloc_local(lz_core_heap_t* heap, uint32_t size_class_idx, uint32_t block_size) {
    lz_slab_bin_t* slab = heap->active_slabs[size_class_idx];

retry_fast_path:
    if (LZ_LIKELY(slab != NULL)) {
        /* 1. Fast-Path: Reciclaje Inmediato vía Free List O(1) */
        if (LZ_LIKELY(slab->free_list != NULL)) {
            lz_free_node_t* node = slab->free_list;
            slab->free_list = (lz_free_node_t*)lz_ptr_obfuscate(node->next, (void**)&node->next);
            slab->used_objects++;
            return (void*)node;
        }

        /* 2. Fast-Path: Inicialización Perezosa vía Bump Pointer O(1) */
        if (LZ_LIKELY(slab->bump_ptr < slab->bump_limit)) {
            void* ptr = (void*)slab->bump_ptr;
            slab->bump_ptr += slab->block_size;
            slab->used_objects++;
            return ptr;
        }

        /* 3. Slow-Path: Slab lleno. Buscar uno parcial en la lista. */
        lz_slab_bin_t* current = slab->next;
        while (current != NULL) {
            if (current->free_list != NULL || current->bump_ptr < current->bump_limit) {
                /* Desvinculación */
                if (current->prev) current->prev->next = current->next;
                if (current->next) current->next->prev = current->prev;
                
                /* Promoción a Head O(1) */
                current->next = heap->active_slabs[size_class_idx];
                current->prev = NULL;
                heap->active_slabs[size_class_idx]->prev = current;
                heap->active_slabs[size_class_idx] = current;
                
                slab = current;
                goto retry_fast_path; /* Evita overhead de recursión (Zero stack growth) */
            }
            current = current->next;
        }
    }

    /* 4. Aprovisionamiento Físico desde el VMM Global */
    lz_chunk_t* chunk = lz_vmm_alloc_chunk(heap->core_id);
    if (LZ_UNLIKELY(!chunk)) return NULL;

    chunk->chunk_type = LZ_CHUNK_TYPE_SLAB;
    lz_pagemap_set_slow(chunk, chunk);

    lz_slab_bin_t* new_slab = (lz_slab_bin_t*)((uint8_t*)chunk + LZ_CACHE_LINE_SIZE);
    uintptr_t payload_base = (uintptr_t)chunk + (LZ_CACHE_LINE_SIZE * 2);
    
    new_slab->bump_ptr = (uint8_t*)payload_base;
    new_slab->bump_limit = (uint8_t*)chunk + LZ_HUGE_PAGE_SIZE;
    new_slab->free_list = NULL;
    
    new_slab->block_size = block_size;
    new_slab->size_class_idx = size_class_idx;
    new_slab->used_objects = 0;

    new_slab->next = heap->active_slabs[size_class_idx];
    new_slab->prev = NULL;
    if (heap->active_slabs[size_class_idx]) {
        heap->active_slabs[size_class_idx]->prev = new_slab;
    }
    heap->active_slabs[size_class_idx] = new_slab;

    LZ_DEBUG("Slab: Aprovisionado nuevo Chunk para Clase de Tamaño %u (Block Size: %u)", size_class_idx, block_size);

    slab = new_slab;
    goto retry_fast_path;
}

void lz_slab_free_local(lz_core_heap_t* heap, lz_chunk_t* chunk, void* ptr) {
    lz_slab_bin_t* slab = (lz_slab_bin_t*)((uint8_t*)chunk + LZ_CACHE_LINE_SIZE);
    lz_free_node_t* node = (lz_free_node_t*)ptr;
    
    node->next = (lz_free_node_t*)lz_ptr_obfuscate(slab->free_list, (void**)&node->next);
    slab->free_list = node;
    slab->used_objects--;

    if (LZ_UNLIKELY(slab->used_objects == 0)) {
        uint32_t target_idx = slab->size_class_idx;
        
        /* Histéresis térmica: Retener el último slab vacío de esta clase. */
        if (heap->active_slabs[target_idx] == slab && slab->next == NULL) {
            return; 
        }

        if (slab->prev) {
            slab->prev->next = slab->next;
        } else {
            heap->active_slabs[target_idx] = slab->next;
        }
        
        if (slab->next) {
            slab->next->prev = slab->prev;
        }

        lz_pagemap_set_slow(chunk, NULL);
        lz_vmm_free_chunk(chunk);
        LZ_DEBUG("Slab: Chunk destruido y devuelto al VMM (Clase: %u)", target_idx);
    }
}