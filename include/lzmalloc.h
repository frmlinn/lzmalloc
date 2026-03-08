/**
 * @file lzmalloc.h
 * @brief Public Interface (Layer 5) of the lzmalloc V2 unified allocator.
 * @details Fully POSIX-compliant interface exposing the triple-hierarchy memory 
 * engines (Slabs, Binned Spans, and Direct Mmap) under a single, seamless API.
 */

#ifndef LZ_MALLOC_H
#define LZ_MALLOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bootstraps the global memory management system (Layers 0, 1, 2).
 * @note Must be explicitly called before any allocation if POSIX hooks are bypassed.
 */
void lz_system_init(void);

/**
 * @brief Allocates a memory block of at least 'size' bytes.
 * @details Automatically routes the request:
 * - <= 32KB: Ultra-fast Slab Engine (O(1)).
 * - 32KB to 1MB: Binned Bitmap Span Engine (Zero internal fragmentation).
 * - > 1MB: Direct OS Mmap Engine via VMM.
 * @param size Requested size in bytes.
 * @return Pointer to the allocated block, or NULL on OOM.
 */
void* lz_malloc(size_t size);

/**
 * @brief Frees a previously allocated memory block.
 * @details Resolves the underlying engine via the lock-free Radix Tree in O(1).
 * Supports remote cross-thread freeing via atomic batching.
 * @param ptr Pointer to the memory block. Safe to pass NULL.
 */
void lz_free(void* ptr);

/**
 * @brief Allocates zero-initialized memory for an array of elements.
 * @param num Number of elements.
 * @param size Size of each element.
 * @return Pointer to the zeroed block, or NULL on overflow/OOM.
 */
void* lz_calloc(size_t num, size_t size);

/**
 * @brief Resizes a previously allocated memory block.
 * @param ptr Pointer to the original block.
 * @param new_size The newly requested size.
 * @return Pointer to the resized block, or NULL on failure.
 */
void* lz_realloc(void* ptr, size_t new_size);

/**
 * @brief Forces an asynchronous Garbage Collection run.
 * @details Iterates over Zombie threads, reaping their remote frees and 
 * returning completely empty Superblocks (Chunks) back to the OS via MADV_DONTNEED.
 */
void lzmalloc_gc(void);

/**
 * @brief Allocates memory with a strict power-of-two alignment boundary.
 * @param alignment Desired alignment.
 * @param size Size in bytes.
 * @return Pointer to the aligned memory, or NULL on error.
 */
void* lz_memalign(size_t alignment, size_t size);

/**
 * @brief POSIX standard compliant aligned allocation.
 * @param memptr Double pointer where the resulting address is stored.
 * @param alignment Desired alignment (Must be multiple of sizeof(void*)).
 * @param size Size in bytes.
 * @return 0 on success, EINVAL/ENOMEM on error.
 */
int lz_posix_memalign(void **memptr, size_t alignment, size_t size);

/**
 * @brief C11 standard compliant aligned allocation.
 */
void* lz_aligned_alloc(size_t alignment, size_t size);

/**
 * @brief Allocates memory aligned to the underlying OS Page Size.
 */
void* lz_valloc(size_t size);

/**
 * @brief Allocates page-aligned memory, rounding the requested size up.
 */
void* lz_pvalloc(size_t size);

/**
 * @brief Introspects the actual backing capacity of an allocated pointer.
 * @details Essential for high-performance C++ containers (std::vector/std::string)
 * to utilize the geometric padding of Slabs and Spans without reallocation.
 * @param ptr Pointer managed by the lzmalloc engine.
 * @return The precise usable size in bytes, or 0 if invalid.
 */
size_t lz_malloc_usable_size(void* ptr);

#ifdef __cplusplus
}
#endif

#endif /* LZ_MALLOC_H */