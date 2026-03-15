/**
 * @file memory.h
 * @brief Operating System Virtual Memory Interface.
 * @details Provides low-level primitives for interacting with the Kernel's 
 * Virtual Memory Manager (VMM), specifically for large-scale memory mappings 
 * and physical memory reclamation.
 */
#ifndef LZ_OS_MEMORY_H
#define LZ_OS_MEMORY_H

#include <stddef.h>
#include "compiler.h"

/* -------------------------------------------------------------------------- *
 * Virtual Memory Area (VMA) API
 * -------------------------------------------------------------------------- */

/**
 * @brief Reserves hyper-aligned virtual memory directly from the kernel.
 * @details Utilizes a "map-and-trim" strategy to guarantee mathematical 
 * alignment boundaries (e.g., 2MB) which are essential for O(1) metadata 
 * resolution via bitmasking.
 * @param size Total requested size (must be a multiple of the alignment).
 * @param alignment Mathematical alignment boundary. Must be a power of 2.
 * @return A pointer guaranteed to meet the alignment criteria, or NULL on 
 * system OOM or invalid parameters.
 */
void* lz_os_alloc_aligned(size_t size, size_t alignment);

/**
 * @brief Releases a virtual mapping and returns physical pages to the OS.
 * @details Performs a standard munmap() operation, effectively destroying 
 * the virtual address range.
 * @param ptr Base pointer of the mapping to be released.
 * @param size Total size of the mapping in bytes.
 */
void lz_os_free(void* ptr, size_t size);

/**
 * @brief RSS Deflation: Releases physical backing without losing the virtual range.
 * @details Informs the kernel that a specific range of pages is no longer 
 * needed. This reduces the process's Resident Set Size (RSS) while keeping 
 * the Virtual Memory Area (VMA) reserved for future use.
 * @param ptr Pointer to the memory region to be purged.
 * @param size Number of bytes to release back to the kernel.
 * @note On Linux, this is a synchronous operation (MADV_DONTNEED).
 */
void lz_os_purge_physical(void* ptr, size_t size);

#endif /* LZ_OS_MEMORY_H */