/**
 * @file atomics.h
 * @brief Primitivas de sincronización estrictas y barreras de memoria.
 * @note Implementado vía macros polimórficas para garantizar el Strict Aliasing.
 */
#ifndef LZ_ATOMICS_H
#define LZ_ATOMICS_H

#include <stdint.h>
#include <stdbool.h>
#include "compiler.h"

/* -------------------------------------------------------------------------- *
 * Cargas / Almacenamiento (Acquire/Release)
 * -------------------------------------------------------------------------- */
#define lz_atomic_load_acquire(ptr) \
    __atomic_load_n((ptr), __ATOMIC_ACQUIRE)

#define lz_atomic_store_release(ptr, val) \
    __atomic_store_n((ptr), (val), __ATOMIC_RELEASE)

/* -------------------------------------------------------------------------- *
 * Fetch-and-Op
 * -------------------------------------------------------------------------- */
#define lz_atomic_fetch_add(ptr, val) \
    __atomic_fetch_add((ptr), (val), __ATOMIC_RELAXED)

#define lz_atomic_fetch_sub(ptr, val) \
    __atomic_fetch_sub((ptr), (val), __ATOMIC_RELAXED)

#define lz_atomic_exchange(ptr, val) \
    __atomic_exchange_n((ptr), (val), __ATOMIC_ACQ_REL)

/* -------------------------------------------------------------------------- *
 * Compare-And-Swap (CAS)
 * -------------------------------------------------------------------------- */

/** * @brief CAS Débil (Polimórfico). Óptimo para bucles (Treiber Stacks).
 * Falla espuriamente pero evita el bloqueo del bus de memoria en ARM (LL/SC).
 */
#define lz_atomic_cas_weak(ptr, expected_ptr, desired) \
    __atomic_compare_exchange_n((ptr), (expected_ptr), (desired), \
                                true, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)

/** * @brief CAS Fuerte (Polimórfico). Para transacciones únicas sin bucle de reintento.
 */
#define lz_atomic_cas_strong(ptr, expected_ptr, desired) \
    __atomic_compare_exchange_n((ptr), (expected_ptr), (desired), \
                                false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)

/* -------------------------------------------------------------------------- *
 * Control de Pipeline de CPU
 * -------------------------------------------------------------------------- */
LZ_ALWAYS_INLINE void lz_cpu_relax(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause" ::: "memory");
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

#endif /* LZ_ATOMICS_H */