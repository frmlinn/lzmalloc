/**
 * @file stress_tests.c
 * @brief High-concurrency, cross-thread stress test.
 * @details Spawns numerous producer and consumer threads to force extreme 
 * contention, lock-free remote frees, and cache-line invalidation scenarios.
 */

#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "lz_log.h"

#define NUM_PRODUCERS 40
#define NUM_CONSUMERS 40
#define ALLOCS_PER_THREAD 10000
#define TOTAL_POINTERS (NUM_PRODUCERS * ALLOCS_PER_THREAD)

/* Lock-free pointer exchange pool with C11 strict memory ordering */
_Atomic(void*) shared_pointers[TOTAL_POINTERS];
_Atomic int write_idx = 0;
_Atomic int read_idx = 0;

/**
 * @brief Producer thread: Frantically allocates memory and pushes it to the shared pool.
 */
void* producer_worker(void* arg) {
    uint64_t tid = (uint64_t)(uintptr_t)arg;
    LZ_DEBUG("Producer %u online.", (unsigned)tid);

    for (int i = 0; i < ALLOCS_PER_THREAD; i++) {
        /* Chaotic sizes: from 8 bytes (Slabs) to 128KB (VMM triggers) */
        size_t size;
        if (rand() % 100 < 99) {
            size = (rand() % 8192) + 8; // Promedio 4KB
        } else {
            size = (rand() % 128000) + 32768; // Gigantes
        }
        void* ptr = malloc(size);
        
        if (LZ_LOG_UNLIKELY(!ptr)) {
            LZ_FATAL("OOM: Producer %u failed to allocate %zu bytes", (unsigned)tid, size);
        }

        /* Force page fault to physically map the memory */
        ((volatile char*)ptr)[0] = 'X';

        /* Lock-free push: Claim index, then publish pointer with RELEASE semantics */
        int idx = __atomic_fetch_add(&write_idx, 1, __ATOMIC_RELAXED);
        __atomic_store_n(&shared_pointers[idx], ptr, __ATOMIC_RELEASE);
    }
    return NULL;
}

/**
 * @brief Consumer thread: Steals pointers allocated by other threads and frees them.
 */
void* consumer_worker(void* arg) {
    uint64_t tid = (uint64_t)(uintptr_t)arg;
    LZ_DEBUG("Consumer %u online.", (unsigned)tid);

    int consumed = 0;
    int target_consumption = TOTAL_POINTERS / NUM_CONSUMERS;

    while (consumed < target_consumption) {
        /* Attempt to claim the next pointer index atomically */
        int claim_idx = __atomic_fetch_add(&read_idx, 1, __ATOMIC_RELAXED);
        
        if (claim_idx < TOTAL_POINTERS) {
            void* ptr_to_free = NULL;
            
            /* Active Spin-Wait with low latency (ACQUIRE semantics). 
             * Wait until the producer actually writes the pointer to the array. */
            while ((ptr_to_free = __atomic_load_n(&shared_pointers[claim_idx], __ATOMIC_ACQUIRE)) == NULL) {
                #if defined(__x86_64__) || defined(__i386__)
                    __asm__ volatile("pause" ::: "memory");
                #elif defined(__aarch64__) || defined(__arm__)
                    __asm__ volatile("yield" ::: "memory");
                #else
                    __asm__ volatile("" ::: "memory");
                #endif
            }
            
            free(ptr_to_free); /* Cross-thread remote free happens HERE */
            consumed++;
        }
    }
    return NULL;
}

int main(void) {
    LZ_INFO("=== CONCURRENCY STRESS TEST ===");
    LZ_INFO("Config: %d Producers, %d Consumers, %d Allocs/Thread", 
            NUM_PRODUCERS, NUM_CONSUMERS, ALLOCS_PER_THREAD);

    pthread_t producers[NUM_PRODUCERS];
    pthread_t consumers[NUM_CONSUMERS];

    /* 1. Unleash the horde */
    for (uintptr_t i = 0; i < NUM_PRODUCERS; i++) {
        pthread_create(&producers[i], NULL, producer_worker, (void*)i);
    }
    for (uintptr_t i = 0; i < NUM_CONSUMERS; i++) {
        pthread_create(&consumers[i], NULL, consumer_worker, (void*)i);
    }

    /* 2. Await the outcome of the battle */
    for (int i = 0; i < NUM_PRODUCERS; i++) pthread_join(producers[i], NULL);
    for (int i = 0; i < NUM_CONSUMERS; i++) pthread_join(consumers[i], NULL);

    LZ_INFO("=== STRESS TEST SURVIVED ===");
    LZ_INFO("Total cross-thread pointers allocated and freed: %d", TOTAL_POINTERS);

    return 0;
}