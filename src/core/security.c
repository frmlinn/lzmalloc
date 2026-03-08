/**
 * @file security.c
 * @brief Implementation of cryptographic security initialization.
 */

#include "security.h"
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

/* ========================================================================= *
 * Global State
 * ========================================================================= */

uintptr_t g_lz_global_secret = 0;

/* ========================================================================= *
 * Implementation
 * ========================================================================= */

void lz_security_init(void) {
    if (LZ_LIKELY(g_lz_global_secret != 0)) {
        return;
    }

    // 1. Primary Path: Cryptographic PRNG from the OS Kernel
    int fd = open("/dev/urandom", O_RDONLY);
    if (LZ_LIKELY(fd != -1)) {
        ssize_t bytes_read = read(fd, &g_lz_global_secret, sizeof(g_lz_global_secret));
        close(fd);
        if (bytes_read == sizeof(g_lz_global_secret) && g_lz_global_secret != 0) {
            return; // Success
        }
    }

    // 2. Secondary Path: High-entropy fallback using ASLR and monotonic time
    uintptr_t stack_var;
    uintptr_t time_val = (uintptr_t)time(NULL);
    uintptr_t pid_val  = (uintptr_t)getpid();
    uintptr_t aslr_val = (uintptr_t)&stack_var;

    g_lz_global_secret = time_val ^ (pid_val << 32) ^ aslr_val;

    // 3. Ultimate Fallback: XORing with 0 essentially disables security
    if (LZ_UNLIKELY(g_lz_global_secret == 0)) {
        g_lz_global_secret = 0xDEADBEEFCAFEBABEULL;
    }
}