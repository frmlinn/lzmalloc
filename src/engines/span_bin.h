/**
 * @file span_bin.h
 * @brief Motor de Spans Segregados para objetos medianos (32KB - 1MB).
 */
#ifndef LZ_ENGINE_SPAN_BIN_H
#define LZ_ENGINE_SPAN_BIN_H

#include "chunk.h"
#include "core_heap.h"
#include <stddef.h>

/**
 * @struct lz_span_bin_t
 * @brief Metadata residente en el offset 0x40 (Línea de caché 2) del Chunk.
 */
typedef struct {
    struct lz_span_bin_s* next;
    struct lz_span_bin_s* prev;

    uint32_t span_size;     
    uint32_t max_spans;     
    uint32_t used_spans;    
    
    /* Bitmap de 64 bits. Soporta hasta 64 particiones. */
    uint64_t usage_bitmap;
    
} LZ_CACHELINE_ALIGNED lz_span_bin_t;

void* lz_span_alloc_local(lz_core_heap_t* heap, uint32_t size_class_idx, size_t exact_size);
void lz_span_free_local(lz_core_heap_t* heap, lz_chunk_t* chunk, void* ptr);

#endif /* LZ_ENGINE_SPAN_BIN_H */