/**
 * @file lzmalloc.h
 * @brief lzmalloc v0.2.0 - High Performance Mechanical Sympathy Allocator
 */
#ifndef LZMALLOC_H
#define LZMALLOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void lz_system_init(void);

void* lz_malloc(size_t size);
void  lz_free(void* ptr);
void* lz_calloc(size_t num, size_t size);
void* lz_realloc(void* ptr, size_t new_size);

void* lz_memalign(size_t alignment, size_t size);
int   lz_posix_memalign(void **memptr, size_t alignment, size_t size);
void* lz_aligned_alloc(size_t alignment, size_t size);
void* lz_valloc(size_t size);
void* lz_pvalloc(size_t size);

size_t lz_malloc_usable_size(void* ptr);

#ifdef __cplusplus
}
#endif

#endif /* LZMALLOC_H */