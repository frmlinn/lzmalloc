/**
 * @file pagemap.c
 * @brief Implementación del Flat Virtual Array para el Slow-Path.
 */
#define _GNU_SOURCE
#include "pagemap.h"
#include "lz_log.h"
#include "atomics.h"
#include <sys/mman.h>
#include <stddef.h>
#include <errno.h>

/* Matemáticas del Espacio de Direcciones (48 bits):
 * Entradas = 2^48 / 2^21 = 134,217,728
 * Tamaño total = 134,217,728 * 8 bytes = 1,073,741,824 bytes (1 GB exacto)
 */
#define PAGEMAP_ENTRIES (1ULL << (48 - LZ_CHUNK_SHIFT))
#define PAGEMAP_SIZE (PAGEMAP_ENTRIES * sizeof(void*))

/* Puntero atómico para inicialización Lock-Free */
static _Atomic(lz_chunk_t**) g_pagemap = NULL;

void lz_pagemap_init(void) {
    lz_chunk_t** current_map = lz_atomic_load_acquire(&g_pagemap);
    if (LZ_LIKELY(current_map != NULL)) return;

    /* MAP_NORESERVE es vital: Informa al kernel que esto es un mapeo disperso (sparse).
     * Consumo físico inicial: 0 bytes. */
    void* map = mmap(
        NULL, PAGEMAP_SIZE, 
        PROT_READ | PROT_WRITE, 
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, 
        -1, 0
    );

    if (LZ_UNLIKELY(map == MAP_FAILED)) {
        LZ_FATAL("Pagemap: mmap() falló al solicitar %llu bytes para el Flat Array (Errno: %d).", 
                 (unsigned long long)PAGEMAP_SIZE, errno);
        return; /* Inalcanzable, LZ_FATAL lanza un trap */
    }

    lz_chunk_t** expected = NULL;
    if (lz_atomic_cas_strong(&g_pagemap, &expected, (lz_chunk_t**)map)) {
        LZ_INFO("Pagemap: Flat Virtual Array inicializado (Reservados %llu MB virtuales).", 
                (unsigned long long)(PAGEMAP_SIZE / 1024 / 1024));
    } else {
        /* Race condition resuelta: Otro hilo se adelantó. Limpiamos nuestro mapeo. */
        LZ_DEBUG("Pagemap: Colisión de inicialización detectada. Liberando mmap redundante.");
        munmap(map, PAGEMAP_SIZE);
    }
}

LZ_COLD_PATH void lz_pagemap_set_slow(void* ptr, lz_chunk_t* meta) {
    lz_chunk_t** map = lz_atomic_load_acquire(&g_pagemap);
    if (LZ_UNLIKELY(map == NULL)) {
        LZ_WARN("Pagemap: Intento de escritura sin inicializar (Set).");
        return;
    }
    
    /* Extraer el índice de 27 bits (Address >> 21) */
    uintptr_t idx = ((uintptr_t)ptr >> LZ_CHUNK_SHIFT) & (PAGEMAP_ENTRIES - 1);
    map[idx] = meta;
}

LZ_COLD_PATH lz_chunk_t* lz_pagemap_get_slow(void* ptr) {
    lz_chunk_t** map = lz_atomic_load_acquire(&g_pagemap);
    if (LZ_UNLIKELY(map == NULL)) return NULL;
    
    uintptr_t idx = ((uintptr_t)ptr >> LZ_CHUNK_SHIFT) & (PAGEMAP_ENTRIES - 1);
    return map[idx];
}