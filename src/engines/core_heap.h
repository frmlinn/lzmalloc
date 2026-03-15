/**
 * @file core_heap.h
 * @brief Arquitectura de Heaps por Núcleo Físico (RSEQ Topology).
 */
#ifndef LZ_ENGINE_CORE_HEAP_H
#define LZ_ENGINE_CORE_HEAP_H

#include "compiler.h"
#include "chunk.h"
#include <stddef.h>
#include <stdint.h>

/* Forward declarations de los motores */
struct lz_slab_bin_s;
struct lz_span_bin_s;

/* -------------------------------------------------------------------------- *
 * Nodo de Buzón (Remote Free)
 * -------------------------------------------------------------------------- */
typedef struct lz_free_node_s {
    struct lz_free_node_s* next; /* Puntero ofuscado (Safe Linking) */
} lz_free_node_t;

/* -------------------------------------------------------------------------- *
 * Core-Local Heap Físico
 * -------------------------------------------------------------------------- */
/**
 * @struct lz_core_heap_t
 * @brief Estado hiper-aislado por núcleo de CPU.
 */
typedef struct {
    /* --- ZONA CALIENTE: LÍNEA DE CACHÉ 1 (Contención Cross-Core) --- */
    /* Buzón MPSC. Aislar este campo en su propia línea de caché es CRÍTICO
     * para evitar que los hilos remotos invaliden las variables del fast-path local. */
    LZ_CACHELINE_ALIGNED uint64_t remote_free_mailbox; 
    
    /* --- ZONA CALIENTE: LÍNEA DE CACHÉ 2+ (Fast Path Local Exclusivo) --- */
    LZ_CACHELINE_ALIGNED uint32_t core_id;
    uint32_t is_initialized;

    struct lz_span_bin_s* active_spans;
    struct lz_slab_bin_s* active_slabs[88];

    /* --- ZONA FRÍA: LÍNEA DE CACHÉ N (Telemetría) --- */
    LZ_CACHELINE_ALIGNED size_t local_bytes_allocated;
    size_t local_bytes_freed;

} LZ_CACHELINE_ALIGNED lz_core_heap_t;

/* -------------------------------------------------------------------------- *
 * API del Core Heap
 * -------------------------------------------------------------------------- */

extern lz_core_heap_t g_core_heaps[LZ_MAX_CORES];

void lz_core_heap_reap_mailbox(lz_core_heap_t* heap);
void lz_core_heap_remote_free(uint32_t target_core_id, void* ptr);

#endif /* LZ_ENGINE_CORE_HEAP_H */