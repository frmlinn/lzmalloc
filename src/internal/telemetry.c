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
#include <stdio.h>
#include <string.h>
#include <stdlib.h> 

/* ========================================================================= *
 * Global State
 * ========================================================================= */

/** @brief Global pointer to the memory-mapped telemetry matrix. */
static lz_stat_slot_t* g_telemetry_matrix = NULL;

/** @brief Process-specific SHM file name. Cached for safe atexit cleanup. */
static char g_shm_name[128] = {0};

/* ========================================================================= *
 * Implementation
 * ========================================================================= */

/**
 * @brief atexit hook to unlink the SHM file upon process termination.
 */
static void lz_telemetry_cleanup(void) {
    if (g_shm_name[0] != '\0') {
        shm_unlink(g_shm_name);
    }
}

void lz_telemetry_init(void) {
    // 1. Guard against double initialization
    if (LZ_UNLIKELY(g_telemetry_matrix != NULL)) {
        return; 
    }

    // 2. Guard: Feature flag check (Missing in original implementation)
    if (getenv("LZMALLOC_TELEMETRY") == NULL) {
        return;
    }

    snprintf(g_shm_name, sizeof(g_shm_name), "/lzmalloc_telemetry_%d", getpid());

    // 3. Create/Open the file in tmpfs (pure RAM)
    int fd = shm_open(g_shm_name, O_CREAT | O_RDWR, 0666);
    if (LZ_UNLIKELY(fd == -1)) {
        return; // Silent fallback: run without telemetry
    }

    size_t matrix_size = sizeof(lz_stat_slot_t) * LZ_MAX_TELEMETRY_SLOTS;
    
    if (LZ_UNLIKELY(ftruncate(fd, (off_t)matrix_size) == -1)) {
        close(fd);
        return;
    }

    // 4. Map the entire matrix into our process memory space
    g_telemetry_matrix = mmap(NULL, matrix_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    
    if (LZ_UNLIKELY(g_telemetry_matrix == MAP_FAILED)) {
        g_telemetry_matrix = NULL;
    } else {
        // Zero-initialize all counters and register cleanup
        memset(g_telemetry_matrix, 0, matrix_size); 
        atexit(lz_telemetry_cleanup); 
    }
    
    close(fd);
}

lz_stat_slot_t* lz_telemetry_get_slot(uint32_t index) {
    if (LZ_UNLIKELY(g_telemetry_matrix == NULL || index >= LZ_MAX_TELEMETRY_SLOTS)) {
        return NULL;
    }
    return &g_telemetry_matrix[index];
}