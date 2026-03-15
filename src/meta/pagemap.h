/**
 * @file pagemap.h
 * @brief Resolución O(1) de Metadata. Híbrido: Bitmask + Flat Array.
 */
#ifndef LZ_PAGEMAP_H
#define LZ_PAGEMAP_H

#include <stdint.h>
#include "compiler.h"
#include "lz_config.h"
#include "chunk.h" /* Integración directa de la Capa 2 */

void lz_pagemap_init(void);
void lz_pagemap_set_slow(void* ptr, lz_chunk_t* meta);
lz_chunk_t* lz_pagemap_get_slow(void* ptr);

/**
 * @brief Resuelve la cabecera del Chunk a partir de cualquier puntero.
 * @note HOT PATH ABSOLUTO. Se ejecutará en cada llamada a lz_free().
 * @warning Asume que el puntero pertenece al espacio de direcciones del proceso.
 * Punteros inválidos o corruptos (wild pointers) causarán un SIGSEGV inmediato
 * por diseño (Fail-Fast).
 */
static LZ_ALWAYS_INLINE lz_chunk_t* lz_meta_resolve(void* ptr) {
    /* 1. FAST-PATH: Bitwise Masking (~0.5 nanosegundos).
     * Aislar el inicio del bloque de 2MB. */
    lz_chunk_t* fast_guess = (lz_chunk_t*)((uintptr_t)ptr & LZ_CHUNK_MASK);

    /* Verificación de integridad: Leer los primeros 8 bytes.
     * Si no coincide, estamos ante un bloque Directo (Slow-Path) o un puntero foráneo. */
    if (LZ_LIKELY(fast_guess && fast_guess->magic == LZ_CHUNK_MAGIC_V2)) {
        return fast_guess;
    }

    /* 2. SLOW-PATH: El puntero no tiene el Magic Number nativo.
     * Buscamos en el árbol de páginas virtual. */
    return lz_pagemap_get_slow(ptr);
}

#endif /* LZ_PAGEMAP_H */