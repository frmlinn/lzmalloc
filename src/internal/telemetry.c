/**
 * @file telemetry.c
 * @brief Implementation of the Shared Memory mapping for passive telemetry.
 */

#define _GNU_SOURCE
#include "telemetry.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

/* ========================================================================= *
 * External Environment Access (Avoiding libc getenv)
 * ========================================================================= */
extern char **environ;

/* ========================================================================= *
 * Global State
 * ========================================================================= */

/** @brief Global pointer to the memory-mapped telemetry matrix. */
static lz_stat_slot_t* g_telemetry_matrix = NULL;

/** @brief Process-specific SHM file name. Cached for fast reference. */
static char g_shm_name[128] = {0};

/* ========================================================================= *
 * Allocation-Free Internal Utilities
 * ========================================================================= */

/**
 * @brief Searches for an environment variable without triggering libc memory allocations.
 */
static bool lz_internal_has_env(const char* key) {
    if (environ == NULL || key == NULL) return false;
    
    size_t key_len = 0;
    while (key[key_len] != '\0') key_len++;

    for (char **env = environ; *env != NULL; env++) {
        if (strncmp(*env, key, key_len) == 0 && (*env)[key_len] == '=') {
            return true;
        }
    }
    return false;
}

/**
 * @brief Converts an integer to a string directly into a buffer without snprintf/malloc.
 */
static void lz_itoa_fast(int val, char* buf, size_t* len) {
    char temp[32];
    int i = 0;
    
    if (val == 0) {
        temp[i++] = '0';
    } else {
        while (val > 0) {
            temp[i++] = (char)((val % 10) + '0');
            val /= 10;
        }
    }
    
    for (int j = 0; j < i; j++) {
        buf[j] = temp[i - 1 - j];
    }
    buf[i] = '\0';
    *len = (size_t)i;
}

/* ========================================================================= *
 * Implementation
 * ========================================================================= */

/**
 * @brief Library destructor. Unlinks the SHM file securely upon process exit.
 * Executed after the main() function completes or during dlclose().
 */
__attribute__((destructor))
static void lz_telemetry_cleanup(void) {
    if (g_shm_name[0] != '\0') {
        shm_unlink(g_shm_name);
    }
}

void lz_telemetry_init(void) {
    if (LZ_UNLIKELY(g_telemetry_matrix != NULL)) {
        return; 
    }

    /* Zero-allocation environment variable check */
    if (!lz_internal_has_env("LZMALLOC_TELEMETRY")) {
        return;
    }

    /* Construct the SHM name manually: "/lzmalloc_telemetry_<PID>" */
    const char* prefix = "/lzmalloc_telemetry_";
    size_t prefix_len = 20;
    memcpy(g_shm_name, prefix, prefix_len);
    
    size_t pid_len = 0;
    lz_itoa_fast(getpid(), g_shm_name + prefix_len, &pid_len);

    /* Create/Open the file in tmpfs (pure RAM) */
    int fd = shm_open(g_shm_name, O_CREAT | O_RDWR, 0666);
    if (LZ_UNLIKELY(fd == -1)) {
        return; 
    }

    size_t matrix_size = sizeof(lz_stat_slot_t) * LZ_MAX_TELEMETRY_SLOTS;
    
    if (LZ_UNLIKELY(ftruncate(fd, (off_t)matrix_size) == -1)) {
        close(fd);
        return;
    }

    /* Map the entire matrix into our process memory space */
    g_telemetry_matrix = mmap(NULL, matrix_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
    
    if (LZ_UNLIKELY(g_telemetry_matrix == MAP_FAILED)) {
        g_telemetry_matrix = NULL;
    } else {
        /* Force memory initialization if MAP_POPULATE is not honored by the OS */
        memset(g_telemetry_matrix, 0, matrix_size); 
    }
    
    close(fd);
}

lz_stat_slot_t* lz_telemetry_get_slot(uint32_t index) {
    if (LZ_UNLIKELY(g_telemetry_matrix == NULL || index >= LZ_MAX_TELEMETRY_SLOTS)) {
        return NULL;
    }
    return &g_telemetry_matrix[index];
}