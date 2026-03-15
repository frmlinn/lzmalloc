/**
 * @file cpu_rt.c
 * @brief Implementación del bypass del kernel para obtener el Core ID.
 */
#define _GNU_SOURCE
#include "cpu_rt.h"
#include "lz_log.h"
#include <sched.h>
#include <unistd.h>
#include <sys/syscall.h>

static __thread uint32_t tls_fallback_core_id = 0;

#ifdef LZ_HAS_RSEQ

#include <linux/rseq.h>

/* RSEQ TLS ABI (usualmente provista por glibc >= 2.35) */
extern __thread volatile struct rseq __rseq_abi __attribute__((weak));

void lz_cpu_rt_thread_init(void) {
    if (LZ_UNLIKELY(!&__rseq_abi)) {
        LZ_WARN("RSEQ ABI ausente o glibc antigua. Cayendo a fallback (sched_getcpu).");
        tls_fallback_core_id = sched_getcpu() % LZ_MAX_CORES;
    } else {
        LZ_DEBUG("RSEQ ABI inyectada con éxito. Fast-path de Core ID activado.");
    }
}

uint32_t lz_cpu_get_core_id(void) {
    if (LZ_LIKELY(&__rseq_abi)) {
        uint32_t core_id = __rseq_abi.cpu_id_start;
        return core_id < LZ_MAX_CORES ? core_id : 0;
    }
    
    unsigned cpu, node;
    /* En glibc modernas getcpu llama al vDSO, previniendo un context-switch completo */
    if (LZ_LIKELY(getcpu(&cpu, &node) == 0)) {
        return cpu % LZ_MAX_CORES;
    }
    return 0;
}

#else /* Darwin o sistemas genéricos sin rseq */

#include <pthread.h>

void lz_cpu_rt_thread_init(void) {
    LZ_INFO("Arquitectura no-Linux detectada. Utilizando hash de pthread_self como Core ID.");
    tls_fallback_core_id = (uint32_t)(((uintptr_t)pthread_self()) % LZ_MAX_CORES);
}

uint32_t lz_cpu_get_core_id(void) {
    return tls_fallback_core_id;
}

#endif