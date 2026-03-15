/**
 * @file routing.h
 * @brief Matemáticas de Size Classes (Fast-Paths).
 */
#ifndef LZ_API_ROUTING_H
#define LZ_API_ROUTING_H

#include "compiler.h"
#include <stdint.h>
#include <stddef.h>

/* Fronteras de los Motores */
#define LZ_MAX_SLAB_SIZE 32768      /* Hasta 32KB -> Slab Engine */
#define LZ_MAX_SPAN_SIZE 1048576    /* Hasta 1MB  -> Span Engine */

/**
 * @brief Traduce un tamaño (bytes) a un size_class_idx (0 a 87).
 */
static LZ_ALWAYS_INLINE void lz_routing_get_size_class(size_t size, uint32_t* out_idx, uint32_t* out_block_size) {
    if (LZ_UNLIKELY(size == 0)) size = 8;
    
    /* Alineación mínima de 8 bytes */
    size = (size + 7) & ~7ULL; 

    if (size <= 256) {
        *out_idx = (uint32_t)((size - 1) >> 3);
        *out_block_size = (uint32_t)size;
        return;
    }

    /* Mapeo logarítmico: Hardware Sympathy BSR */
    uint32_t msb = 63 - (uint32_t)__builtin_clzll(size - 1);
    uint32_t shift = msb - 3;
    uint32_t base_size = 1U << msb;
    uint32_t offset = (uint32_t)(((size - 1) - base_size) >> shift);
    
    *out_block_size = base_size + ((offset + 1) << shift);
    *out_idx = (msb << 3) + offset - 32; 
}

#endif /* LZ_API_ROUTING_H */