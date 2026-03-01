/**
 * @file telemetry.c
 * @brief Implementation of the Shared Memory mapping for passive telemetry.
 */

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

static lz_stat_slot_t* g_telemetry_matrix = NULL;

/* ========================================================================= *
 * Implementation
 * ========================================================================= */

/**
 * @brief atexit hook to clean up the SHM file when the process terminates.
 */
static void lz_telemetry_cleanup(void) {
    char shm_name[128];
    // Predictable format bound to the current Process ID
    snprintf(shm_name, sizeof(shm_name), "/lzmalloc_telemetry_%d", getpid());
    shm_unlink(shm_name);
}

void lz_telemetry_init(void) {
    if (g_telemetry_matrix != NULL) return; // Prevent double initialization

    char shm_name[128];
    snprintf(shm_name, sizeof(shm_name), "/lzmalloc_telemetry_%d", getpid());

    // Create/Open the file in tmpfs (pure RAM)
    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (LZ_UNLIKELY(fd == -1)) return; // Silent fallback: run without telemetry

    size_t size = sizeof(lz_stat_slot_t) * LZ_MAX_TELEMETRY_SLOTS;
    if (LZ_UNLIKELY(ftruncate(fd, size) == -1)) {
        close(fd);
        return;
    }

    // Map the entire matrix into our process memory space
    g_telemetry_matrix = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (LZ_UNLIKELY(g_telemetry_matrix == MAP_FAILED)) {
        g_telemetry_matrix = NULL;
    } else {
        // Zero-initialize all counters
        memset(g_telemetry_matrix, 0, size); 
    }
    
    close(fd);
    
    // Register the cleanup hook to prevent /dev/shm leaks
    atexit(lz_telemetry_cleanup); 
}

lz_stat_slot_t* lz_telemetry_get_slot(uint32_t index) {
    if (LZ_UNLIKELY(!g_telemetry_matrix || index >= LZ_MAX_TELEMETRY_SLOTS)) {
        return NULL;
    }
    return &g_telemetry_matrix[index];
}