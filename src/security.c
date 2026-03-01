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

// Initialized to 0 in the BSS section.
uintptr_t g_lz_global_secret = 0;

/* ========================================================================= *
 * Implementation
 * ========================================================================= */

void lz_security_init(void) {
    if (LZ_LIKELY(g_lz_global_secret != 0)) return;

    // 1. Primary Attempt: Cryptographic PRNG from the OS Kernel
    int fd = open("/dev/urandom", O_RDONLY);
    if (LZ_LIKELY(fd != -1)) {
        ssize_t bytes_read = read(fd, &g_lz_global_secret, sizeof(g_lz_global_secret));
        close(fd);
        if (bytes_read == sizeof(g_lz_global_secret) && g_lz_global_secret != 0) {
            return; // Success: Cryptographically secure seed acquired
        }
    }

    // 2. Secondary Attempt: High-entropy fallback using ASLR and time
    // We XOR the current time with the address of a local variable (ASLR randomized)
    // and the Process ID to ensure uniqueness.
    uintptr_t stack_var;
    uintptr_t time_val = (uintptr_t)time(NULL);
    uintptr_t pid_val = (uintptr_t)getpid();
    uintptr_t aslr_val = (uintptr_t)&stack_var;

    g_lz_global_secret = time_val ^ (pid_val << 32) ^ aslr_val;

    // 3. Ultimate Fallback: Never leave the secret as 0 (XOR 0 disables security)
    if (LZ_UNLIKELY(g_lz_global_secret == 0)) {
        g_lz_global_secret = 0xDEADBEEFCAFEBABEULL;
    }
}