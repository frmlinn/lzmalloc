/**
 * @file posix_hooks.c
 * @brief POSIX calls interception with secure bootstrap via Constructor.
 */

#define _GNU_SOURCE
#include "lzmalloc.h"
#include "common.h"
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#include <unistd.h> 

#define LZ_PUBLIC __attribute__((visibility("default")))

/* ========================================================================= *
 * Layer 0: The Base Allocator (4MB Emergency Static Allocator)
 * ========================================================================= */

#define LZ_BASE_HEAP_SIZE (4 * 1024 * 1024) 
#define LZ_ABI_ALIGNMENT 16 

typedef struct {
    size_t exact_size;
    size_t _padding;
} lz_base_header_t;

static char g_base_heap[LZ_BASE_HEAP_SIZE] __attribute__((aligned(LZ_ABI_ALIGNMENT)));
static _Atomic size_t g_base_offset = 0;

static inline int is_base_ptr(void* ptr) {
    return ((char*)ptr >= g_base_heap) && ((char*)ptr < g_base_heap + LZ_BASE_HEAP_SIZE);
}

static void* lz_base_malloc(size_t size) {
    if (size == 0) size = LZ_ABI_ALIGNMENT;

    size_t total_size = LZ_ALIGN_UP(size + sizeof(lz_base_header_t), LZ_ABI_ALIGNMENT);
    size_t old_offset = atomic_fetch_add_explicit(&g_base_offset, total_size, memory_order_relaxed);
    
    if (LZ_UNLIKELY(old_offset + total_size > LZ_BASE_HEAP_SIZE)) {
        char msg[] = "\n[LZMALLOC FATAL] 4MB Base Allocator exhausted during bootstrap.\n";
        #pragma GCC diagnostic ignored "-Wunused-result"
        write(2, msg, sizeof(msg) - 1);
        __builtin_trap(); 
    }

    lz_base_header_t* header = (lz_base_header_t*)(g_base_heap + old_offset);
    header->exact_size = size;
    return (void*)(header + 1); 
}

/* ========================================================================= *
 * Control Layer: Magic Initialization
 * ========================================================================= */

static _Atomic int g_lz_ready = 0;

__attribute__((constructor)) 
static void lz_bootstrap_system(void) {
    lz_system_init(); 
    atomic_store_explicit(&g_lz_ready, 1, memory_order_release);
}

/* ========================================================================= *
 * Internal Routing Wrapper (Lock-Free, No Syscalls)
 * ========================================================================= */

static inline void* lz_malloc_routed(size_t size) {
    if (LZ_LIKELY(atomic_load_explicit(&g_lz_ready, memory_order_relaxed) == 1)) {
        return lz_malloc(size);
    }
    return lz_base_malloc(size);
}

static inline void lz_free_routed(void* ptr) {
    if (LZ_UNLIKELY(!ptr)) return;
    if (LZ_UNLIKELY(is_base_ptr(ptr))) return;

    if (LZ_LIKELY(atomic_load_explicit(&g_lz_ready, memory_order_relaxed) == 1)) {
        lz_free(ptr);
    }
}

/* ========================================================================= *
 * PUBLIC HOOKS
 * ========================================================================= */

LZ_PUBLIC void* malloc(size_t size) {
    return lz_malloc_routed(size);
}

LZ_PUBLIC void free(void* ptr) {
    lz_free_routed(ptr);
}

LZ_PUBLIC void* calloc(size_t num, size_t size) {
    size_t total = num * size;
    if (LZ_LIKELY(atomic_load_explicit(&g_lz_ready, memory_order_relaxed) == 1)) {
        return lz_calloc(num, size);
    }
    void* ptr = lz_base_malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

LZ_PUBLIC void* realloc(void* ptr, size_t new_size) {
    if (!ptr) return lz_malloc_routed(new_size);
    if (new_size == 0) { lz_free_routed(ptr); return NULL; }

    if (LZ_UNLIKELY(is_base_ptr(ptr))) {
        lz_base_header_t* header = (lz_base_header_t*)ptr - 1; 
        size_t old_size = header->exact_size; 
        if (new_size <= old_size) return ptr;

        void* new_ptr = lz_malloc_routed(new_size);
        if (new_ptr) memcpy(new_ptr, ptr, old_size);
        return new_ptr;
    }

    if (LZ_UNLIKELY(atomic_load_explicit(&g_lz_ready, memory_order_relaxed) == 0)) {
        return NULL; 
    }

    return lz_realloc(ptr, new_size);
}

LZ_PUBLIC int posix_memalign(void **memptr, size_t alignment, size_t size) {
    if (LZ_LIKELY(atomic_load_explicit(&g_lz_ready, memory_order_relaxed) == 1)) {
        return lz_posix_memalign(memptr, alignment, size);
    }
    *memptr = lz_base_malloc(size);
    return 0;
}

LZ_PUBLIC void* memalign(size_t alignment, size_t size) {
    if (LZ_LIKELY(atomic_load_explicit(&g_lz_ready, memory_order_relaxed) == 1)) {
        return lz_memalign(alignment, size);
    }
    return lz_base_malloc(size);
}

LZ_PUBLIC void* aligned_alloc(size_t alignment, size_t size) {
    if (LZ_LIKELY(atomic_load_explicit(&g_lz_ready, memory_order_relaxed) == 1)) {
        return lz_aligned_alloc(alignment, size);
    }
    return lz_base_malloc(size);
}

LZ_PUBLIC void* valloc(size_t size) {
    if (LZ_LIKELY(atomic_load_explicit(&g_lz_ready, memory_order_relaxed) == 1)) {
        return lz_valloc(size);
    }
    return lz_base_malloc(size);
}

LZ_PUBLIC void* pvalloc(size_t size) {
    return valloc(size);
}

LZ_PUBLIC size_t malloc_usable_size(void* ptr) {
    if (!ptr) return 0;
    if (LZ_UNLIKELY(is_base_ptr(ptr))) {
        lz_base_header_t* header = (lz_base_header_t*)ptr - 1; 
        return header->exact_size; 
    }
    if (LZ_LIKELY(atomic_load_explicit(&g_lz_ready, memory_order_relaxed) == 1)) {
        return lz_malloc_usable_size(ptr);
    }
    return 0;
}

/* ========================================================================= *
 * TOTAL DOMINATION: GLIBC internal symbols overwrite (NSS/Systemd)
 * ========================================================================= */
LZ_PUBLIC void* __libc_malloc(size_t size) { return malloc(size); }
LZ_PUBLIC void  __libc_free(void* ptr) { free(ptr); }
LZ_PUBLIC void* __libc_realloc(void* ptr, size_t size) { return realloc(ptr, size); }
LZ_PUBLIC void* __libc_calloc(size_t n, size_t size) { return calloc(n, size); }
LZ_PUBLIC void  __libc_cfree(void* ptr) { free(ptr); }
LZ_PUBLIC void* __libc_memalign(size_t align, size_t size) { return memalign(align, size); }

LZ_PUBLIC void* reallocarray(void* ptr, size_t nmemb, size_t size) {
    size_t total;
    if (LZ_UNLIKELY(__builtin_mul_overflow(nmemb, size, &total))) return NULL;
    return realloc(ptr, total);
}

LZ_PUBLIC void cfree(void *ptr) {
    free(ptr);
}