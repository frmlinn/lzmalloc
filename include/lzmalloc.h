/**
 * @file lzmalloc.h
 * @brief Public Interface (Layer 5) of the lzmalloc V2 general allocator.
 * Compatible with the standard POSIX memory management API.
 */

#ifndef LZ_MALLOC_H
#define LZ_MALLOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the global system (Layers 0, 1, 2).
 * Must be called before any memory allocation occurs.
 */
void lz_system_init(void);

/**
 * @brief Allocates a memory block of at least 'size' bytes.
 * @param size Size in bytes.
 * @return Pointer to the allocated block, or NULL if 'size' is 0 or system is OOM.
 */
void* lz_malloc(size_t size);

/**
 * @brief Frees a previously allocated memory block.
 * @param ptr Pointer to the block. If NULL, the function does nothing.
 */
void lz_free(void* ptr);

/**
 * @brief Allocates memory for an array of 'num' elements of 'size' bytes each.
 * The returned memory is zero-initialized.
 * @param num Number of elements.
 * @param size Size of each element.
 * @return Pointer to the zeroed block, or NULL on error or overflow.
 */
void* lz_calloc(size_t num, size_t size);

/**
 * @brief Resizes a previously allocated memory block.
 * Attempts an in-place resize if the Size Class allows it.
 * @param ptr Pointer to the original block.
 * @param new_size The new desired size.
 * @return Pointer to the new block (might be the same as 'ptr'), or NULL on failure.
 */
void* lz_realloc(void* ptr, size_t new_size);

/**
 * @brief Forces a Garbage Collection run in the allocator.
 * Iterates through dead threads (Zombies), processing their pending frees
 * and returning empty memory back to the Operating System.
 * @note Ideal for calling from background threads (e.g., compaction threads).
 */
void lzmalloc_gc(void);

/**
 * @brief Allocates memory with a strict alignment boundary.
 * @details Forwards small, naturally aligned requests to the Slab engine. 
 * For large or strictly aligned requests (> 64 bytes), it bypasses the TLH
 * and requests a Huge Page directly from the VMM, dynamically calculating
 * the payload offset to satisfy the alignment while preserving the chunk header.
 * * @param alignment Desired alignment (must be a power of 2).
 * @param size Size in bytes to allocate.
 * @return Pointer to the aligned memory, or NULL on error.
 */
void* lz_memalign(size_t alignment, size_t size);

/**
 * @brief Allocates aligned memory guaranteeing the modern POSIX standard.
 * @param memptr Double indirection pointer where the result will be stored.
 * @param alignment Alignment (must be a power of 2 and a multiple of sizeof(void*)).
 * @param size Size to allocate.
 * @return 0 on success, EINVAL if alignment is invalid, ENOMEM if OOM.
 */
int lz_posix_memalign(void **memptr, size_t alignment, size_t size);

/**
 * @brief Allocates aligned memory (C11 Standard).
 * @param alignment Desired alignment (must be a power of 2).
 * @param size Size to allocate (should be a multiple of alignment according to C11).
 * @return Pointer to the aligned memory, or NULL on error.
 */
void* lz_aligned_alloc(size_t alignment, size_t size);

/**
 * @brief Allocates memory aligned to the OS Page Size.
 * @param size Size to allocate.
 * @return Pointer to page-aligned memory, or NULL.
 */
void* lz_valloc(size_t size);

/**
 * @brief Allocates page-aligned memory and rounds the size up to a multiple of the page size.
 * @param size Base size to allocate.
 * @return Pointer to aligned and rounded memory, or NULL.
 */
void* lz_pvalloc(size_t size);

/**
 * @brief Returns the actual size of the memory block backing the pointer.
 * Essential for std::vector and std::string implementations in C++.
 * @param ptr Pointer previously allocated by the POSIX engine.
 * @return The total usable size in bytes, or 0 if the pointer is invalid/foreign.
 */
size_t lz_malloc_usable_size(void* ptr);

#ifdef __cplusplus
}
#endif

#endif // LZ_MALLOC_H