/**
 * @file atomics.h
 * @brief Memory Consistency Primitives.
 * @details Implements strict synchronization and memory barriers using compiler
 * built-ins (__atomic_*). This ensures complete independence from libatomic.
 * @note All CAS operations follow the C11 memory model.
 */
#ifndef LZ_ATOMICS_H
#define LZ_ATOMICS_H

#include <stdint.h>
#include <stdbool.h>
#include "compiler.h"

/**
 * @brief Atomic Load with Acquire semantics.
 * @details Ensures subsequent reads/writes are not reordered before this load.
 */
#define lz_atomic_load_acquire(ptr) \
    __atomic_load_n((ptr), __ATOMIC_ACQUIRE)

/**
 * @brief Atomic Store with Release semantics.
 * @details Ensures previous reads/writes are visible to other cores before the store.
 */
#define lz_atomic_store_release(ptr, val) \
    __atomic_store_n((ptr), (val), __ATOMIC_RELEASE)

/* Fetch-and-Op Primitives (Relaxed) */

/** @brief Atomic increment (Relaxed). Suitable for telemetry counters. */
#define lz_atomic_fetch_add(ptr, val) \
    __atomic_fetch_add((ptr), (val), __ATOMIC_RELAXED)

/** @brief Atomic decrement (Relaxed). Suitable for telemetry counters. */
#define lz_atomic_fetch_sub(ptr, val) \
    __atomic_fetch_sub((ptr), (val), __ATOMIC_RELAXED)

/** @brief Atomic Exchange (Acquire/Release). Atomically updates a value. */
#define lz_atomic_exchange(ptr, val) \
    __atomic_exchange_n((ptr), (val), __ATOMIC_ACQ_REL)

/* Compare-And-Swap (CAS) Primitives */

/**
 * @brief Weak Compare-And-Swap.
 * @details May fail spuriously. Best used in loops where performance is prioritized.
 * @param ptr Pointer to the target value.
 * @param expected_ptr Pointer to the value expected in ptr.
 * @param desired The new value to store if successful.
 * @return true if successful, false otherwise.
 */
#define lz_atomic_cas_weak(ptr, expected_ptr, desired) \
    __atomic_compare_exchange_n((ptr), (expected_ptr), (desired), \
                                true, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)

/**
 * @brief Strong Compare-And-Swap.
 * @details Guarantees failure only if values differ.
 */
#define lz_atomic_cas_strong(ptr, expected_ptr, desired) \
    __atomic_compare_exchange_n((ptr), (expected_ptr), (desired), \
                                false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)

/**
 * @brief CPU Pipeline Relax.
 * @details Emits a hint to the processor (PAUSE/YIELD) during busy-wait loops 
 * to reduce power consumption and memory bus contention.
 */
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