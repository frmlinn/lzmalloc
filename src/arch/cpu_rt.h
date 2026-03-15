/**
 * @file cpu_rt.h
 * @brief Topological Runtime Interface.
 * @details Provides fast-path identification of the current CPU core ID via 
 * RSEQ ABI or vDSO fallback.
 */
#ifndef LZ_CPU_RT_H
#define LZ_CPU_RT_H

#include <stdint.h>
#include "compiler.h"

/**
 * @brief Initializes the per-thread topological runtime.
 * @details Attempts to bind the thread to the RSEQ ABI or caches a fallback core ID.
 */
void lz_cpu_rt_thread_init(void);

/**
 * @brief Retrieves the current Core ID with zero-syscall latency.
 * @return The 0-indexed physical core ID.
 * @note If RSEQ is unavailable, it falls back to a vDSO call.
 */
uint32_t lz_cpu_get_core_id(void);

#endif /* LZ_CPU_RT_H */