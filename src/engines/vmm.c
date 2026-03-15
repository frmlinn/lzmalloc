/**
 * @file vmm.c
 * @brief Implementación del Treiber Stack con ABA-Tagging en 64-bits.
 */
#include "vmm.h"
#include "memory.h"
#include "atomics.h"
#include "lz_log.h"

#define ABA_PTR_MASK  0x0000FFFFFFFFFFFFULL
#define ABA_TAG_SHIFT 48
#define ABA_TAG_ADD   (1ULL << ABA_TAG_SHIFT)

static LZ_ALWAYS_INLINE lz_chunk_t* aba_unpack_ptr(uint64_t tagged) {
    return (lz_chunk_t*)(tagged & ABA_PTR_MASK);
}

static LZ_ALWAYS_INLINE uint64_t aba_pack(lz_chunk_t* ptr, uint64_t old_tagged) {
    uint64_t new_tag = (old_tagged + ABA_TAG_ADD) & ~ABA_PTR_MASK;
    return new_tag | (uintptr_t)ptr;
}

typedef struct {
    LZ_CACHELINE_ALIGNED uint64_t lf_stack_head;
    LZ_CACHELINE_ALIGNED uint32_t cached_count; 
} LZ_CACHELINE_ALIGNED vmm_pool_t;

static vmm_pool_t g_vmm_pool;

void lz_vmm_init(void) {
    lz_atomic_store_release(&g_vmm_pool.lf_stack_head, 0);
    lz_atomic_store_release(&g_vmm_pool.cached_count, 0);
    LZ_INFO("VMM: Lock-free pool inicializado.");
}

lz_chunk_t* lz_vmm_alloc_chunk(uint32_t core_id) {
    uint64_t current_head = lz_atomic_load_acquire(&g_vmm_pool.lf_stack_head);
    lz_chunk_t* chunk;

    /* Treiber Stack (Pop) */
    do {
        chunk = aba_unpack_ptr(current_head);
        if (LZ_UNLIKELY(!chunk)) break;
        
        /* Mitigación de Data-Race en C11: Lectura atómica especulativa. 
         * No hay SIGSEGV gracias a que nunca aplicamos munmap() a chunks cacheados. */
        lz_chunk_t* next_chunk = (lz_chunk_t*)lz_atomic_load_acquire((uintptr_t*)&chunk->next);
        uint64_t new_head = aba_pack(next_chunk, current_head);

        if (lz_atomic_cas_weak(&g_vmm_pool.lf_stack_head, &current_head, new_head)) {
            lz_atomic_fetch_sub(&g_vmm_pool.cached_count, 1);
            break;
        }
    } while (true);

    if (LZ_UNLIKELY(!chunk)) {
        chunk = (lz_chunk_t*)lz_os_alloc_aligned(LZ_HUGE_PAGE_SIZE, LZ_HUGE_PAGE_SIZE);
        if (LZ_UNLIKELY(!chunk)) {
            LZ_ERROR("VMM: OS OOM. Imposible adquirir Chunk de 2MB.");
            return NULL;
        }
    }

    chunk->magic = LZ_CHUNK_MAGIC_V2;
    chunk->core_id = core_id;
    chunk->next = NULL;
    
    return chunk;
}

void lz_vmm_free_chunk(lz_chunk_t* chunk) {
    if (LZ_UNLIKELY(!chunk || chunk->magic != LZ_CHUNK_MAGIC_V2)) return;

    /* RSS Deflation: Si excede el límite, devolvemos memoria física pero RETENEMOS 
     * el mapeo virtual y la cabecera, de lo contrario la lectura especulativa en 
     * el Pop lanzaría un SIGSEGV. */
    if (lz_atomic_load_acquire(&g_vmm_pool.cached_count) > LZ_VMM_MAX_CACHED_CHUNKS) {
        void* payload_start = (uint8_t*)chunk + LZ_PAGE_SIZE;
        size_t payload_size = LZ_HUGE_PAGE_SIZE - LZ_PAGE_SIZE;
        lz_os_purge_physical(payload_start, payload_size);
        LZ_DEBUG("VMM: Límite excedido. %zu bytes de RSS físico devueltos al OS.", payload_size);
    }

    uint64_t current_head = lz_atomic_load_acquire(&g_vmm_pool.lf_stack_head);
    uint64_t new_head;
    do {
        /* Grabamos especulativamente el next */
        lz_atomic_store_release((uintptr_t*)&chunk->next, (uintptr_t)aba_unpack_ptr(current_head));
        new_head = aba_pack(chunk, current_head);
    } while (!lz_atomic_cas_weak(&g_vmm_pool.lf_stack_head, &current_head, new_head));

    lz_atomic_fetch_add(&g_vmm_pool.cached_count, 1);
}