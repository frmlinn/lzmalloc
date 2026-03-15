/**
 * @file pagemap.c
 * @brief Implementation of the 512GB Sparse Virtual Array.
 * @details Utilizes the MAP_NORESERVE flag to allocate a massive virtual 
 * range without committing physical RAM, relying on the kernel's demand 
 * paging to manage metadata storage.
 */
#define _GNU_SOURCE
#include "pagemap.h"
#include "lz_log.h"
#include <sys/mman.h>
#include <stddef.h>
#include <errno.h>

/**
 * @brief Total virtual size of the pagemap.
 * Calculation: 2^36 entries * 8 bytes/pointer = 512 GB.
 */
#define PAGEMAP_SIZE (PAGEMAP_ENTRIES * sizeof(void*))

/** @internal Global atomic root for the pagemap. */
_Atomic(lz_chunk_t**) g_pagemap = NULL;

void lz_pagemap_init(void) {
    /* Double-Checked Locking (DCL) optimized via atomics */
    lz_chunk_t** current_map = lz_atomic_load_acquire(&g_pagemap);
    if (LZ_LIKELY(current_map != NULL)) return;

    /**
     * @details MAP_NORESERVE is critical: it prevents the OS from checking 
     * available swap/RAM for the 512GB allocation, allowing us to reserve 
     * the VA space solely for address translation.
     */
    void* map = mmap(
        NULL, PAGEMAP_SIZE, 
        PROT_READ | PROT_WRITE, 
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, 
        -1, 0
    );

    if (LZ_UNLIKELY(map == MAP_FAILED)) {
        LZ_FATAL("Pagemap: mmap() failed to reserve 512GB VA (Errno: %d).", errno);
        return; 
    }

    lz_chunk_t** expected = NULL;
    /* Atomic CAS to handle concurrent initialization during LD_PRELOAD bootstrap */
    if (lz_atomic_cas_strong(&g_pagemap, &expected, (lz_chunk_t**)map)) {
        LZ_INFO("Pagemap: Flat Virtual Array initialized (%llu MB VA reserved).", 
                (unsigned long long)(PAGEMAP_SIZE / 1024 / 1024));
    } else {
        /* lost race: free the redundant mapping */
        munmap(map, PAGEMAP_SIZE);
    }
}

LZ_COLD_PATH void lz_pagemap_set_slow(void* ptr, lz_chunk_t* meta) {
    lz_chunk_t** map = lz_atomic_load_acquire(&g_pagemap);
    if (LZ_UNLIKELY(map == NULL)) return;
    
    uintptr_t idx = ((uintptr_t)ptr >> LZ_CHUNK_SHIFT) & (PAGEMAP_ENTRIES - 1);
    map[idx] = meta;
}

LZ_COLD_PATH lz_chunk_t* lz_pagemap_get_slow(void* ptr) {
    lz_chunk_t** map = lz_atomic_load_acquire(&g_pagemap);
    if (LZ_UNLIKELY(map == NULL)) return NULL;
    
    uintptr_t idx = ((uintptr_t)ptr >> LZ_CHUNK_SHIFT) & (PAGEMAP_ENTRIES - 1);
    return map[idx];
}