/**
 * @file vmm.h
 * @brief Virtual Memory Manager (VMM) Lock-Free.
 */
#ifndef LZ_ENGINE_VMM_H
#define LZ_ENGINE_VMM_H

#include "chunk.h"
#include <stdint.h>

#define LZ_VMM_MAX_CACHED_CHUNKS 32

void lz_vmm_init(void);

/**
 * @brief Extrae un Chunk de 2MB. $O(1)$ si hay caché lock-free. $O(N)$ (syscall mmap) si el pool está vacío.
 */
lz_chunk_t* lz_vmm_alloc_chunk(uint32_t core_id);
void lz_vmm_free_chunk(lz_chunk_t* chunk);

#endif /* LZ_ENGINE_VMM_H */