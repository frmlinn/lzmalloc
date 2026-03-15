/**
 * @file cpu_rt.c
 * @brief Kernel Bypass implementation for Core ID detection.
 * @details Implements the fast-path core identification by mapping into the 
 * RSEQ ABI provided by glibc >= 2.35.
 */
#define _GNU_SOURCE
#include "cpu_rt.h"
#include "lz_log.h"
#include <sched.h>
#include <unistd.h>
#include <sys/syscall.h>

/** @brief Thread-local cache for systems without RSEQ support. */
static __thread uint32_t tls_fallback_core_id __attribute__((tls_model("initial-exec"))) = 0;

#ifdef LZ_HAS_RSEQ

#include <linux/rseq.h>

/** * @brief Pointer to the RSEQ TLS ABI structure.
 * @note Weak linkage allows the allocator to work on older glibc versions. 
 */
extern __thread volatile struct rseq __rseq_abi __attribute__((weak, tls_model("initial-exec")));

void lz_cpu_rt_thread_init(void) {
    if (LZ_UNLIKELY(!&__rseq_abi)) {
        LZ_WARN("RSEQ ABI missing or obsolete glibc. Falling back to vDSO/sched_getcpu.");
        tls_fallback_core_id = sched_getcpu() % LZ_MAX_CORES;
    } else {
        LZ_DEBUG("RSEQ ABI successfully injected. Enabling core-local fast-path.");
    }
}

uint32_t lz_cpu_get_core_id(void) {
    if (LZ_LIKELY(&__rseq_abi)) {
        /* Direct access to the kernel-mapped CPU ID field */
        uint32_t core_id = __rseq_abi.cpu_id_start;
        return core_id < LZ_MAX_CORES ? core_id : 0;
    }
    
    unsigned cpu, node;
    /* Modern glibc getcpu() routes to the vDSO, avoiding a full ring-0 transition */
    if (LZ_LIKELY(getcpu(&cpu, &node) == 0)) {
        return cpu % LZ_MAX_CORES;
    }
    return 0;
}

#else /* Non-Linux or Generic Unix Profiles */

#include <pthread.h>

void lz_cpu_rt_thread_init(void) {
    LZ_INFO("Non-Linux architecture. Using pthread_self hash as Core ID surrogate.");
    tls_fallback_core_id = (uint32_t)(((uintptr_t)pthread_self()) % LZ_MAX_CORES);
}

uint32_t lz_cpu_get_core_id(void) {
    return tls_fallback_core_id;
}

#endif