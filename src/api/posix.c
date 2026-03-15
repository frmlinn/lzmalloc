/**
 * @file posix.c
 * @brief LD_PRELOAD Hooks y Base Allocator de Supervivencia.
 */
#include "lzmalloc.h"
#include "atomics.h"
#include "lz_log.h"
#include "lz_config.h"
#include <string.h>
#include <unistd.h>

#define LZ_PUBLIC __attribute__((visibility("default")))

/* ========================================================================= *
 * LAYER 0: The Base Allocator (4MB Survival Buffer)
 * ========================================================================= */
#define LZ_BASE_HEAP_SIZE (4 * 1024 * 1024) 
#define LZ_ABI_ALIGNMENT 16 

typedef struct {
    size_t exact_size;
    size_t _padding;
} lz_base_header_t;

static char g_base_heap[LZ_BASE_HEAP_SIZE] LZ_CACHELINE_ALIGNED;
static uint64_t g_base_offset = 0;

static LZ_ALWAYS_INLINE int is_base_ptr(void* ptr) {
    return ((char*)ptr >= g_base_heap) && ((char*)ptr < g_base_heap + LZ_BASE_HEAP_SIZE);
}

static void* lz_base_malloc_aligned(size_t alignment, size_t size) {
    if (alignment < LZ_ABI_ALIGNMENT) alignment = LZ_ABI_ALIGNMENT;
    if (size == 0) size = LZ_ABI_ALIGNMENT;

    uint64_t current_offset = lz_atomic_load_acquire(&g_base_offset);
    uint64_t aligned_offset, total_size;
    
    do {
        uintptr_t base_addr = (uintptr_t)g_base_heap + current_offset;
        uintptr_t payload_addr = (base_addr + sizeof(lz_base_header_t) + alignment - 1) & ~(alignment - 1);
        aligned_offset = payload_addr - sizeof(lz_base_header_t) - (uintptr_t)g_base_heap;
        total_size = (size + LZ_ABI_ALIGNMENT - 1) & ~(LZ_ABI_ALIGNMENT - 1);
        
        if (LZ_UNLIKELY(aligned_offset + sizeof(lz_base_header_t) + total_size > LZ_BASE_HEAP_SIZE)) {
            char msg[] = "\n[LZMALLOC FATAL] 4MB Base Allocator exhausted during bootstrap.\n";
            write(STDERR_FILENO, msg, sizeof(msg) - 1);
            __builtin_trap(); 
        }
    } while (!lz_atomic_cas_weak(&g_base_offset, &current_offset, aligned_offset + sizeof(lz_base_header_t) + total_size));

    lz_base_header_t* header = (lz_base_header_t*)(g_base_heap + aligned_offset);
    header->exact_size = size;
    return (void*)(header + 1);
}

/* ========================================================================= *
 * CONTROL LAYER: Magic Initialization
 * ========================================================================= */
static uint32_t g_lz_ready = 0;

__attribute__((constructor)) 
static void lz_bootstrap_system(void) {
    lz_system_init(); 
    lz_atomic_store_release(&g_lz_ready, 1);
}

/* ========================================================================= *
 * HOOKS PÚBLICOS
 * ========================================================================= */

LZ_PUBLIC void* malloc(size_t size) {
    if (LZ_LIKELY(lz_atomic_load_acquire(&g_lz_ready) == 1)) {
        return lz_malloc(size);
    }
    return lz_base_malloc_aligned(LZ_ABI_ALIGNMENT, size);
}

LZ_PUBLIC void free(void* ptr) {
    if (LZ_UNLIKELY(!ptr)) return;
    if (LZ_UNLIKELY(is_base_ptr(ptr))) return;

    if (LZ_LIKELY(lz_atomic_load_acquire(&g_lz_ready) == 1)) {
        lz_free(ptr);
    }
}

LZ_PUBLIC void* calloc(size_t num, size_t size) {
    size_t total;
    if (LZ_UNLIKELY(__builtin_mul_overflow(num, size, &total))) return NULL;
    
    void* ptr = malloc(total);
    if (LZ_LIKELY(ptr)) {
        memset(ptr, 0, total);
    }
    return ptr;
}

LZ_PUBLIC void* realloc(void* ptr, size_t new_size) {
    if (!ptr) return malloc(new_size);
    if (new_size == 0) { free(ptr); return NULL; }

    if (LZ_UNLIKELY(is_base_ptr(ptr))) {
        lz_base_header_t* header = (lz_base_header_t*)ptr - 1; 
        if (new_size <= header->exact_size) return ptr;

        void* new_ptr = malloc(new_size);
        if (new_ptr) {
            memcpy(new_ptr, ptr, header->exact_size);
            LZ_INFO("POSIX: Migrado objeto desde Survival Base Heap al Main Heap.");
        }
        return new_ptr;
    }

    if (LZ_LIKELY(lz_atomic_load_acquire(&g_lz_ready) == 1)) {
        return lz_realloc(ptr, new_size); 
    }
    return NULL;
}

/* Resto de hooks (memalign, valloc, etc.) */
LZ_PUBLIC int posix_memalign(void **memptr, size_t alignment, size_t size) {
    if (alignment % sizeof(void*) != 0 || (alignment & (alignment - 1)) != 0) return 22; 
    if (LZ_LIKELY(lz_atomic_load_acquire(&g_lz_ready) == 1)) {
        void* ptr = lz_memalign(alignment, size);
        if (!ptr) return 12; 
        *memptr = ptr;
        return 0;
    }
    *memptr = lz_base_malloc_aligned(alignment, size);
    return *memptr ? 0 : 12;
}

LZ_PUBLIC void* memalign(size_t alignment, size_t size) {
    if (LZ_LIKELY(lz_atomic_load_acquire(&g_lz_ready) == 1)) return lz_memalign(alignment, size);
    return lz_base_malloc_aligned(alignment, size);
}

LZ_PUBLIC void* aligned_alloc(size_t alignment, size_t size) {
    return memalign(alignment, size);
}

LZ_PUBLIC void* valloc(size_t size) {
    if (LZ_LIKELY(lz_atomic_load_acquire(&g_lz_ready) == 1)) return lz_valloc(size);
    return lz_base_malloc_aligned(LZ_PAGE_SIZE, size);
}

LZ_PUBLIC void* pvalloc(size_t size) {
    if (LZ_LIKELY(lz_atomic_load_acquire(&g_lz_ready) == 1)) return lz_pvalloc(size);
    size_t rounded_size = (size + LZ_PAGE_SIZE - 1) & ~(LZ_PAGE_SIZE - 1);
    return lz_base_malloc_aligned(LZ_PAGE_SIZE, rounded_size);
}

LZ_PUBLIC size_t malloc_usable_size(void* ptr) {
    if (!ptr) return 0;
    if (LZ_UNLIKELY(is_base_ptr(ptr))) {
        lz_base_header_t* header = (lz_base_header_t*)ptr - 1; 
        return header->exact_size; 
    }
    if (LZ_LIKELY(lz_atomic_load_acquire(&g_lz_ready) == 1)) {
        return lz_malloc_usable_size(ptr);
    }
    return 0;
}

LZ_PUBLIC void cfree(void *ptr) { free(ptr); }

/* GLIBC Overwrites */
LZ_PUBLIC void* __libc_malloc(size_t size) { return malloc(size); }
LZ_PUBLIC void  __libc_free(void* ptr) { free(ptr); }
LZ_PUBLIC void* __libc_calloc(size_t n, size_t size) { return calloc(n, size); }
LZ_PUBLIC void* __libc_realloc(void* ptr, size_t size) { return realloc(ptr, size); }
LZ_PUBLIC void* __libc_memalign(size_t align, size_t size) { return memalign(align, size); }
LZ_PUBLIC void* __libc_valloc(size_t size) { return valloc(size); }
LZ_PUBLIC void* __libc_pvalloc(size_t size) { return pvalloc(size); }
LZ_PUBLIC void  __libc_cfree(void* ptr) { free(ptr); }