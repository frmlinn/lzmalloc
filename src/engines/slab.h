/**
 * @file slab.h
 * @brief Motor de Slabs para objetos pequeños (<= 32KB).
 */
#ifndef LZ_ENGINE_SLAB_H
#define LZ_ENGINE_SLAB_H

#include "chunk.h"
#include "core_heap.h"

/**
 * @struct lz_slab_bin_t
 * @brief Cabecera del Slab residente en la segunda línea de caché del Chunk.
 */
typedef struct {
    struct lz_slab_bin_s* next;
    struct lz_slab_bin_s* prev;

    uint8_t* bump_ptr;      
    uint8_t* bump_limit;    
    lz_free_node_t* free_list; 

    uint32_t block_size;     
    uint32_t size_class_idx; 
    uint32_t used_objects;   
    
    /* El compilador inyectará automáticamente el padding exacto aquí 
     * gracias a LZ_CACHELINE_ALIGNED, evitando errores matemáticos humanos. */
} LZ_CACHELINE_ALIGNED lz_slab_bin_t;

void* lz_slab_alloc_local(lz_core_heap_t* heap, uint32_t size_class_idx, uint32_t block_size);
void lz_slab_free_local(lz_core_heap_t* heap, lz_chunk_t* chunk, void* ptr);

#endif /* LZ_ENGINE_SLAB_H */