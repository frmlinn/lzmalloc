/**
 * @file vmm.c
 * @brief Lock-Free Virtual Memory Manager via Index-Based Virtual Arena.
 * @details Implements a 1TB Virtual Arena strategy. Chunks are managed using 
 * an ABA-resistant Treiber Stack where pointers are replaced by 32-bit indices 
 * to allow 64-bit native atomic operations.
 */
#define _GNU_SOURCE
#include "vmm.h"
#include "memory.h"
#include "atomics.h"
#include "lz_log.h"
#include <sys/mman.h>
#include <errno.h>

/** @brief 1 TB Virtual Arena capacity (524,288 chunks of 2MB). */
#define LZ_ARENA_MAX_CHUNKS 524288
/** @brief Total Virtual Address (VA) space reserved for the arena. */
#define LZ_ARENA_SIZE ((size_t)LZ_ARENA_MAX_CHUNKS * LZ_HUGE_PAGE_SIZE)

/**
 * @union lf_head_t
 * @brief 64-bit packed structure for native atomic Treiber Stack operations.
 * @details Combines a 32-bit chunk index and a 32-bit ABA tag to prevent 
 * the ABA problem without requiring CMPXCHG16B.
 */
typedef union {
    struct {
        uint32_t chunk_id; /**< Index-based pointer (ID 0 represents NULL). */
        uint32_t aba_tag;  /**< Monotonic counter to detect concurrent mutations. */
    } data;
    uint64_t raw;          /**< Raw 64-bit value for atomic CAS. */
} lf_head_t;

/**
 * @struct vmm_pool_t
 * @brief Global state for the Virtual Memory Manager.
 */
typedef struct {
    /** @brief Atomic head of the recycled chunk stack. */
    LZ_CACHELINE_ALIGNED _Atomic uint64_t lf_stack_head;
    /** @brief Number of chunks currently residing in the stack. */
    LZ_CACHELINE_ALIGNED uint32_t cached_count;
    
    /* Virtual Arena State */
    /** @brief Base address of the 1TB hyper-aligned mapping. */
    LZ_CACHELINE_ALIGNED uintptr_t arena_base;
    /** @brief Monotonic counter for cutting new chunks from the arena (Bump-ptr). */
    _Atomic uint32_t bump_id;
} vmm_pool_t;

/** @internal Static global instance of the VMM pool. */
static vmm_pool_t g_vmm_pool;

/**
 * @brief O(1) Translation: ID to Virtual Pointer.
 * @param id The 32-bit chunk index.
 * @return Pointer to the hyper-aligned lz_chunk_t header.
 */
static LZ_ALWAYS_INLINE lz_chunk_t* id_to_ptr(uint32_t id) {
    if (LZ_UNLIKELY(id == 0)) return NULL;
    return (lz_chunk_t*)(g_vmm_pool.arena_base + ((uintptr_t)(id - 1) << LZ_CHUNK_SHIFT));
}

/**
 * @brief O(1) Translation: Virtual Pointer to ID.
 * @param ptr Pointer to the chunk header.
 * @return The corresponding 32-bit index.
 */
static LZ_ALWAYS_INLINE uint32_t ptr_to_id(lz_chunk_t* ptr) {
    if (LZ_UNLIKELY(!ptr)) return 0;
    return (uint32_t)(((uintptr_t)ptr - g_vmm_pool.arena_base) >> LZ_CHUNK_SHIFT) + 1;
}

void lz_vmm_init(void) {
    /** * @details Maps 1TB + 2MB extra to guarantee mathematical hyper-alignment 
     * via manual trimming. MAP_NORESERVE ensures zero physical RAM pressure. 
     */
    void* raw_arena = mmap(NULL, LZ_ARENA_SIZE + LZ_HUGE_PAGE_SIZE, 
                           PROT_READ | PROT_WRITE, 
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    
    if (LZ_UNLIKELY(raw_arena == MAP_FAILED)) {
        LZ_FATAL("VMM: Failed to map 1TB Virtual Arena (Errno: %d).", errno);
        __builtin_trap();
    }

    uintptr_t raw_addr = (uintptr_t)raw_arena;
    uintptr_t aligned_addr = (raw_addr + LZ_HUGE_PAGE_SIZE - 1) & LZ_CHUNK_MASK;
    
    /* Trim prefix and suffix excess to maintain a clean VMA footprint */
    size_t prefix_size = aligned_addr - raw_addr;
    if (prefix_size > 0) munmap((void*)raw_addr, prefix_size);
    
    size_t suffix_size = LZ_HUGE_PAGE_SIZE - prefix_size;
    if (suffix_size > 0) munmap((void*)(aligned_addr + LZ_ARENA_SIZE), suffix_size);

    g_vmm_pool.arena_base = aligned_addr;
    __atomic_store_n(&g_vmm_pool.bump_id, 1, __ATOMIC_RELEASE); 
    
    lf_head_t initial_head = { .data = {0, 0} };
    __atomic_store_n(&g_vmm_pool.lf_stack_head, initial_head.raw, __ATOMIC_RELEASE);
    __atomic_store_n(&g_vmm_pool.cached_count, 0, __ATOMIC_RELEASE);
    
    LZ_INFO("VMM: Lock-Free Virtual Arena (Index-Based 64-bit) mapped at %p.", (void*)aligned_addr);
}

lz_chunk_t* lz_vmm_alloc_chunk(uint32_t core_id) {
    uint64_t current_val = __atomic_load_n(&g_vmm_pool.lf_stack_head, __ATOMIC_ACQUIRE);
    lf_head_t current_head;
    lz_chunk_t* chunk = NULL;

    /* 1. Fast-Path: Lock-Free Pop from Treiber Stack */
    do {
        current_head.raw = current_val;
        if (current_head.data.chunk_id == 0) {
            break; /* Stack is empty, move to slow-path */
        }
        
        chunk = id_to_ptr(current_head.data.chunk_id);
        uint32_t next_id = __atomic_load_n(&chunk->next_id, __ATOMIC_ACQUIRE);
        
        lf_head_t new_head = { .data = { .chunk_id = next_id, .aba_tag = current_head.data.aba_tag + 1 } };

        if (__atomic_compare_exchange_n(&g_vmm_pool.lf_stack_head, &current_val, new_head.raw, 
                                        true, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            __atomic_fetch_sub(&g_vmm_pool.cached_count, 1, __ATOMIC_RELAXED);
            break;
        }
    } while (true);

    /* 2. Slow-Path: Atomic Bump-Allocation from the Virtual Arena */
    if (LZ_UNLIKELY(current_head.data.chunk_id == 0)) {
        uint32_t new_id = __atomic_fetch_add(&g_vmm_pool.bump_id, 1, __ATOMIC_RELAXED);
        
        if (LZ_UNLIKELY(new_id > LZ_ARENA_MAX_CHUNKS)) {
            LZ_FATAL("VMM: Virtual Arena Exhausted (1TB consumed).");
            __builtin_trap();
        }
        
        chunk = id_to_ptr(new_id);
        
#if defined(__linux__) && defined(MADV_HUGEPAGE)
        /* Advisory: Enable THP for the newly cut chunk */
        madvise(chunk, LZ_HUGE_PAGE_SIZE, MADV_HUGEPAGE);
#endif
    }

    /* Initialize Metadata */
    chunk->magic = LZ_CHUNK_MAGIC_V2;
    chunk->core_id = core_id;
    chunk->next_id = 0;
    
    return chunk;
}

void lz_vmm_free_chunk(lz_chunk_t* chunk) {
    if (LZ_UNLIKELY(!chunk || chunk->magic != LZ_CHUNK_MAGIC_V2)) return;

    /* RSS Deflation: Return physical memory if the cache is full */
    if (__atomic_load_n(&g_vmm_pool.cached_count, __ATOMIC_RELAXED) > LZ_VMM_MAX_CACHED_CHUNKS) {
        void* payload_start = (uint8_t*)chunk + LZ_PAGE_SIZE;
        size_t payload_size = LZ_HUGE_PAGE_SIZE - LZ_PAGE_SIZE;
        lz_os_purge_physical(payload_start, payload_size);
    }

    uint32_t chunk_id = ptr_to_id(chunk);
    uint64_t current_val = __atomic_load_n(&g_vmm_pool.lf_stack_head, __ATOMIC_ACQUIRE);
    lf_head_t current_head;
    lf_head_t new_head;

    /* 3. Lock-Free Push: Returns the chunk to the stack for future reuse */
    do {
        current_head.raw = current_val;
        __atomic_store_n(&chunk->next_id, current_head.data.chunk_id, __ATOMIC_RELEASE);
        
        new_head.data.chunk_id = chunk_id;
        new_head.data.aba_tag = current_head.data.aba_tag + 1;

    } while (!__atomic_compare_exchange_n(&g_vmm_pool.lf_stack_head, &current_val, new_head.raw, 
                                          true, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));

    __atomic_fetch_add(&g_vmm_pool.cached_count, 1, __ATOMIC_RELAXED);
}