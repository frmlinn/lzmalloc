/**
 * @file security.c
 * @brief Implementación de la adquisición de entropía a nivel OS.
 */
#define _GNU_SOURCE
#include "security.h"
#include "lz_log.h"
#include "atomics.h"
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/syscall.h>

uintptr_t g_lz_global_secret = 0;

void lz_security_init(void) {
    /* Fast-path check: Evitar locks si ya está inicializado */
    uintptr_t expected = lz_atomic_load_acquire(&g_lz_global_secret);
    if (LZ_LIKELY(expected != 0)) {
        return;
    }

    uintptr_t secret = 0;

    /* 1. Fast-Path OS: Llamada al sistema directa */
#if defined(__linux__) && defined(SYS_getrandom)
    if (syscall(SYS_getrandom, &secret, sizeof(secret), 0) == sizeof(secret)) {
        if (secret != 0) {
            LZ_DEBUG("Security: Entropía adquirida vía SYS_getrandom.");
            goto commit_secret;
        }
    }
#endif

    /* 2. Slow-Path OS: Lectura de /dev/urandom */
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd != -1) {
        ssize_t bytes_read = read(fd, &secret, sizeof(secret));
        close(fd);
        if (bytes_read == sizeof(secret) && secret != 0) {
            LZ_DEBUG("Security: Entropía adquirida vía /dev/urandom.");
            goto commit_secret;
        }
    }

    /* 3. Fallback de Emergencia: Mezcla de ASLR y tiempo monotónico */
    LZ_WARN("Security: Fallo en CSPRNG del OS. Usando fallback de baja entropía.");
    uintptr_t stack_var;
    uintptr_t time_val = (uintptr_t)time(NULL);
    uintptr_t pid_val  = (uintptr_t)getpid();
    uintptr_t aslr_val = (uintptr_t)&stack_var;

    /* Evitar shift de 32 bits en arquitecturas de 32 bits (UB) */
    secret = time_val ^ (pid_val << 16) ^ aslr_val;

    if (LZ_UNLIKELY(secret == 0)) {
        secret = (uintptr_t)0xDEADBEEF; /* Fallback seguro en 32/64 bits */
    }

commit_secret:
    /* Resolución Lock-Free: Si dos hilos llegan aquí, solo el ganador escribe la semilla */
    if (lz_atomic_cas_strong(&g_lz_global_secret, &expected, secret)) {
        LZ_INFO("Security: Safe Linking activado con éxito.");
    }
}