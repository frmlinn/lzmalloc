/**
 * @file cpu_rt.h
 * @brief Runtime Topológico. Detección de CPU ID vía RSEQ o vDSO.
 */
#ifndef LZ_CPU_RT_H
#define LZ_CPU_RT_H

#include <stdint.h>
#include "compiler.h"

void lz_cpu_rt_thread_init(void);

/**
 * @brief Obtiene el Core ID actual. 
 * @note LTO (Link-Time Optimization) inyectará esto en el llamador.
 */
uint32_t lz_cpu_get_core_id(void);

#endif /* LZ_CPU_RT_H */