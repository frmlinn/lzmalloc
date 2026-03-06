/**
 * @file vmm.c
 * @brief Virtual Memory Manager con Extent Allocation (Adiós a la Tormenta VMA).
 */

#define _GNU_SOURCE
#include "vmm.h"
#include <sys/mman.h>
#include <stdatomic.h>
#include <unistd.h>

/* ========================================================================= *
 * Configuración de Extents (Super-Bloques)
 * ========================================================================= */

// Pedimos 64MB de golpe al Kernel (32 Chunks de 2MB)
#define LZ_EXTENT_CHUNKS 32
#define LZ_EXTENT_SIZE (LZ_HUGE_PAGE_SIZE * LZ_EXTENT_CHUNKS)

/* ========================================================================= *
 * Internal State: Global Pool & NUMA Locks
 * ========================================================================= */

static atomic_flag g_pool_locks[LZ_MAX_NUMA_NODES];

// Global Chunk Pool (Slow-path buffer)
static atomic_flag g_global_pool_lock = ATOMIC_FLAG_INIT;
static lz_chunk_header_t* g_global_free_chunks = NULL;

/* ========================================================================= *
 * Helpers: Spinlocks
 * ========================================================================= */

static LZ_ALWAYS_INLINE void pool_lock(uint32_t node_id) {
    while (atomic_flag_test_and_set_explicit(&g_pool_locks[node_id], memory_order_acquire)) {
        lz_cpu_relax();
    }
}

static LZ_ALWAYS_INLINE void pool_unlock(uint32_t node_id) {
    atomic_flag_clear_explicit(&g_pool_locks[node_id], memory_order_release);
}

static LZ_ALWAYS_INLINE void global_pool_lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_global_pool_lock, memory_order_acquire)) {
        lz_cpu_relax();
    }
}

static LZ_ALWAYS_INLINE void global_pool_unlock(void) {
    atomic_flag_clear_explicit(&g_global_pool_lock, memory_order_release);
}

/* ========================================================================= *
 * Extent Allocation (The VMA Saver)
 * ========================================================================= */

/**
 * @brief Pide 64MB al SO, los alinea y los trocea en 32 Chunks de 2MB.
 * Reduce drásticamente las llamadas a mmap y elimina la fragmentación VMA.
 */
static void sys_alloc_extent(void) {
    // Pedimos el Extent + 2MB extra para garantizar la alineación
    size_t request_size = LZ_EXTENT_SIZE + LZ_HUGE_PAGE_SIZE;
    
    void* raw_ptr = mmap(NULL, request_size, PROT_READ | PROT_WRITE, 
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                         
    if (LZ_UNLIKELY(raw_ptr == MAP_FAILED)) return;

    uintptr_t raw_addr = (uintptr_t)raw_ptr;
    uintptr_t aligned_addr = raw_addr;

    // Solo recortamos si el Kernel no nos dio una dirección mágicamente alineada
    if (LZ_UNLIKELY((raw_addr & (LZ_HUGE_PAGE_SIZE - 1)) != 0)) {
        aligned_addr = LZ_ALIGN_UP(raw_addr, LZ_HUGE_PAGE_SIZE);
        size_t prefix_size = aligned_addr - raw_addr;
        size_t suffix_size = request_size - prefix_size - LZ_EXTENT_SIZE;

        if (prefix_size > 0) munmap((void*)raw_addr, prefix_size);
        if (suffix_size > 0) munmap((void*)(aligned_addr + LZ_EXTENT_SIZE), suffix_size);
    } else {
        // Devolvemos el margen de 2MB extra que no usamos
        munmap((void*)(raw_addr + LZ_EXTENT_SIZE), LZ_HUGE_PAGE_SIZE);
    }

#ifdef MADV_HUGEPAGE
    madvise((void*)aligned_addr, LZ_EXTENT_SIZE, MADV_HUGEPAGE);
#endif

    // Trocear el Extent en Chunks de 2MB y meterlos al Global Pool
    global_pool_lock();
    for (size_t i = 0; i < LZ_EXTENT_CHUNKS; ++i) {
        lz_chunk_header_t* chunk = (lz_chunk_header_t*)(aligned_addr + (i * LZ_HUGE_PAGE_SIZE));
        chunk->next = g_global_free_chunks;
        g_global_free_chunks = chunk;
    }
    global_pool_unlock();
}

/* ========================================================================= *
 * Public API Implementation
 * ========================================================================= */

void lz_vmm_init(void) {
    lz_topology_init();
    for (int i = 0; i < LZ_MAX_NUMA_NODES; ++i) {
        atomic_flag_clear(&g_pool_locks[i]);
    }
    atomic_flag_clear(&g_global_pool_lock);
}

lz_chunk_header_t* lz_vmm_alloc_chunk(void) {
    uint32_t current_node = lz_get_current_node();
    lz_numa_pool_t* pool = lz_topology_get_pool(current_node);
    lz_chunk_header_t* chunk = NULL;

    // 1. FAST PATH: Intentar robar del pool NUMA local
    pool_lock(current_node);
    lz_chunk_header_t* top = atomic_load_explicit(&pool->free_chunks, memory_order_relaxed);
    if (top) {
        chunk = top;
        atomic_store_explicit(&pool->free_chunks, chunk->next, memory_order_relaxed);
        atomic_fetch_sub_explicit(&pool->available_count, 1, memory_order_relaxed);
    }
    pool_unlock(current_node);

    if (LZ_LIKELY(chunk != NULL)) {
        chunk->next = NULL;
        return chunk;
    }

    // 2. SLOW PATH: Ir al Global Pool
    global_pool_lock();
    if (!g_global_free_chunks) {
        global_pool_unlock();
        sys_alloc_extent(); // Rellenar la reserva
        global_pool_lock();
    }
    
    if (g_global_free_chunks) {
        chunk = g_global_free_chunks;
        g_global_free_chunks = chunk->next;
    }
    global_pool_unlock();

    if (LZ_UNLIKELY(!chunk)) return NULL; // Out of Memory Real

    // Inicialización neutral
    chunk->next = NULL;
    chunk->owning_tlh = NULL; 
    chunk->node_id = current_node;
    chunk->is_lsm_region = 0;
    chunk->magic = LZ_CHUNK_MAGIC_V2; 
    chunk->canary = 0;
    chunk->checksum = 0;

    return chunk;
}

void lz_vmm_free_chunk(lz_chunk_header_t* chunk) {
    if (LZ_UNLIKELY(!chunk || chunk->magic != LZ_CHUNK_MAGIC_V2)) return; 

    uint32_t target_node = chunk->node_id;
    lz_numa_pool_t* pool = lz_topology_get_pool(target_node);
    bool return_to_global = false;

    // 1. Histéresis térmica: Intentar dejarlo en el caché NUMA rápido (Sin purgar)
    // Esto previene los Soft Page Faults en cargas de trabajo de alta frecuencia.
    pool_lock(target_node);
    size_t count = atomic_load_explicit(&pool->available_count, memory_order_relaxed);
    if (count < LZ_VMM_MAX_CACHED_CHUNKS) {
        lz_chunk_header_t* top = atomic_load_explicit(&pool->free_chunks, memory_order_relaxed);
        chunk->next = top;
        atomic_store_explicit(&pool->free_chunks, chunk, memory_order_relaxed);
        atomic_fetch_add_explicit(&pool->available_count, 1, memory_order_relaxed);
    } else {
        // El caché caliente está lleno. Este Chunk está frío.
        return_to_global = true;
    }
    pool_unlock(target_node);

    // 2. Deflación de RSS: Devolver al Global Pool purgando la memoria física
    if (LZ_UNLIKELY(return_to_global)) {
        
        // LA TÉCNICA DE LA PÁGINA SEGURA:
        // Calculamos el inicio de la carga útil (Payload) saltando la primera 
        // página del sistema operativo para proteger el lz_chunk_header_t.
        void* payload_start = (char*)chunk + LZ_PAGE_SIZE;
        size_t payload_size = LZ_HUGE_PAGE_SIZE - LZ_PAGE_SIZE;

        // Avisamos al SO que reclame la RAM física. 
        // El mapa virtual (VMA) permanece intacto.
#if defined(__linux__)
        // En Linux, DONTNEED actúa de forma síncrona sobre el RSS. Bajarán los números de top/htop al instante.
        madvise(payload_start, payload_size, MADV_DONTNEED);
#elif defined(__APPLE__) || defined(__FreeBSD__)
        // En BSD/macOS, FREE es más gentil. Marca la memoria como descartable bajo presión de RAM.
        madvise(payload_start, payload_size, MADV_FREE);
#endif

        // Inserción segura lock-free en el Global Pool
        global_pool_lock();
        chunk->next = g_global_free_chunks;
        g_global_free_chunks = chunk;
        global_pool_unlock();
    }
}