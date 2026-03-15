/**
 * @file security.c
 * @brief Implementation of OS-level entropy acquisition.
 * @details Cascades through available entropy sources: Hardware RNG -> CSPRNG -> 
 * Software Fallback.
 */
#define _GNU_SOURCE
#include "security.h"
#include "lz_log.h"
#include "atomics.h"
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/syscall.h>

/** @internal Secret storage initialized to zero. */
uintptr_t g_lz_global_secret = 0;

void lz_security_init(void) {
    /* Fast-path: Check if already initialized with Acquire semantics */
    uintptr_t expected = lz_atomic_load_acquire(&g_lz_global_secret);
    if (LZ_LIKELY(expected != 0)) {
        return;
    }

    uintptr_t secret = 0;

    /* 1. Fast-Path: Kernel Syscall (Linux 3.17+) */
#if defined(__linux__) && defined(SYS_getrandom)
    if (syscall(SYS_getrandom, &secret, sizeof(secret), 0) == sizeof(secret)) {
        if (secret != 0) {
            LZ_DEBUG("Security: Entropy acquired via SYS_getrandom.");
            goto commit_secret;
        }
    }
#endif

    /* 2. Slow-Path: Read from /dev/urandom device */
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd != -1) {
        ssize_t bytes_read = read(fd, &secret, sizeof(secret));
        close(fd);
        if (bytes_read == sizeof(secret) && secret != 0) {
            LZ_DEBUG("Security: Entropy acquired via /dev/urandom.");
            goto commit_secret;
        }
    }

    /* 3. Emergency Fallback: Mix ASLR address and Monotonic Time */
    LZ_WARN("Security: OS CSPRNG failure. Using low-entropy fallback.");
    uintptr_t stack_var;
    uintptr_t time_val = (uintptr_t)time(NULL);
    uintptr_t pid_val  = (uintptr_t)getpid();
    uintptr_t aslr_val = (uintptr_t)&stack_var;

    /* XOR-Shift mix to generate a non-zero seed */
    secret = time_val ^ (pid_val << 16) ^ aslr_val;

    if (LZ_UNLIKELY(secret == 0)) {
        secret = (uintptr_t)0xDEADBEEF; /* Deterministic safety fallback */
    }

commit_secret:
    /* Lock-Free Resolution: Concurrent threads race; only one writes the seed.
     * Uses Strong CAS to ensure memory consistency. */
    if (lz_atomic_cas_strong(&g_lz_global_secret, &expected, secret)) {
        LZ_INFO("Security: Safe Linking mitigation enabled.");
    }
}