/**
 * @file lzmalloc.h
 * @brief lzmalloc v0.2.0 - High Performance Mechanical Sympathy Allocator.
 * @details Public API for the lzmalloc engine. This allocator uses a 
 * triple-hierarchy strategy (Slab/Span/Direct) with core-affined heaps 
 * and RSEQ-based synchronization to achieve O(1) latency.
 */
#ifndef LZMALLOC_H
#define LZMALLOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Manually initializes the global allocator state.
 * @note This is called automatically by the constructor in posix.c, 
 * but can be called manually for specific embedded use cases.
 */
void lz_system_init(void);

/**
 * @brief Standard memory allocation.
 * @param size Number of bytes to allocate.
 * @return Pointer to the allocated memory, or NULL if the system is OOM.
 */
void* lz_malloc(size_t size);

/**
 * @brief Standard memory deallocation.
 * @param ptr Pointer to the memory block to release.
 * @note Supports remote deallocation via MPSC mailboxes if the block 
 * was allocated on a different physical core.
 */
void  lz_free(void* ptr);

/**
 * @brief Allocates and zeroes memory.
 * @param num Number of elements.
 * @param size Size of each element.
 * @return Zero-initialized pointer or NULL.
 */
void* lz_calloc(size_t num, size_t size);

/**
 * @brief Resizes an existing memory block.
 * @param ptr Original pointer.
 * @param new_size Target size in bytes.
 * @return New pointer (may be the same as ptr) or NULL.
 */
void* lz_realloc(void* ptr, size_t new_size);

/* -------------------------------------------------------------------------- *
 * Aligned Allocation API
 * -------------------------------------------------------------------------- */

/** @brief Allocates memory with a specific alignment. */
void* lz_memalign(size_t alignment, size_t size);

/** @brief POSIX-compliant aligned allocation. */
int   lz_posix_memalign(void **memptr, size_t alignment, size_t size);

/** @brief C11-compliant aligned allocation. */
void* lz_aligned_alloc(size_t alignment, size_t size);

/** @brief Allocates memory aligned to a system page. */
void* lz_valloc(size_t size);

/** @brief Allocates memory aligned to a system page, rounded up to a page. */
void* lz_pvalloc(size_t size);

/**
 * @brief Query the actual usable size of a pointer.
 * @details Returns the exact footprint of the block (Slab size or Span size).
 */
size_t lz_malloc_usable_size(void* ptr);

#ifdef __cplusplus
}
#endif

#endif /* LZMALLOC_H */