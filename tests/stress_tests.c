/**
 * @file stress_tests.c
 * @brief High-concurrency, cross-thread stress test for lzmalloc V2.
 * @details Spawns numerous producer and consumer threads to force extreme 
 * contention, lock-free remote frees, and cache-line invalidation scenarios
 * across all three memory engines.
 */

#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdatomic.h>
#include <sched.h>
#include "lz_log.h"
#include "lzmalloc.h"

#define NUM_PRODUCERS 40
#define NUM_CONSUMERS 40
#define ALLOCS_PER_THREAD 10000
#define TOTAL_POINTERS (NUM_PRODUCERS * ALLOCS_PER_THREAD)

/* Lock-free pointer exchange pool with strict C11 memory ordering */
_Atomic(void*) shared_pointers[TOTAL_POINTERS];
_Atomic int write_idx = 0;
_Atomic int read_idx = 0;

/**
 * @brief Producer thread: Frantically allocates across all engines and pushes to shared pool.
 */
void* producer_worker(void* arg) {
    uintptr_t tid = (uintptr_t)arg;
    LZ_DEBUG("Producer %u online.", (unsigned)tid);

    for (int i = 0; i < ALLOCS_PER_THREAD; i++) {
        size_t size;
        int r = rand() % 100;
        
        /* Chaotic sizing to bombard the triple-hierarchy routing */
        if (r < 80) {
            size = (rand() % 32700) + 8;         /* 80%: Slabs (up to ~32KB) */
        } else if (r < 98) {
            size = (rand() % 900000) + 32769;    /* 18%: Spans (32KB - 1MB) */
        } else {
            size = (rand() % 4000000) + 1048577; /* 2%: Direct Mmap (> 1MB) */
        }
        
        void* ptr = malloc(size);
        
        if (LZ_LOG_UNLIKELY(!ptr)) {
            LZ_FATAL("OOM: Producer %u failed to allocate %zu bytes", (unsigned)tid, size);
        }

        /* Force a hard page fault to physically map the memory in the OS */
        ((volatile char*)ptr)[0] = 'X';

        /* Lock-free push: Claim index, then publish pointer with RELEASE semantics */
        int idx = atomic_fetch_add_explicit(&write_idx, 1, memory_order_relaxed);
        atomic_store_explicit(&shared_pointers[idx], ptr, memory_order_release);
    }
    return NULL;
}

/**
 * @brief Consumer thread: Steals pointers allocated by other threads and frees them remotely.
 */
void* consumer_worker(void* arg) {
    uintptr_t tid = (uintptr_t)arg;
    LZ_DEBUG("Consumer %u online.", (unsigned)tid);

    int consumed = 0;
    int target_consumption = TOTAL_POINTERS / NUM_CONSUMERS;

    while (consumed < target_consumption) {
        int claim_idx = atomic_fetch_add_explicit(&read_idx, 1, memory_order_relaxed);
        
        if (claim_idx < TOTAL_POINTERS) {
            void* ptr_to_free = NULL;
            
            /* Active wait loop for the producer to publish the pointer */
            while ((ptr_to_free = atomic_load_explicit(&shared_pointers[claim_idx], memory_order_acquire)) == NULL) {
                sched_yield();
            }
            
            free(ptr_to_free); /* Triggers the remote batching engine */
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

    /* Initialize the global array to strictly NULL */
    for (int i = 0; i < TOTAL_POINTERS; i++) {
        atomic_init(&shared_pointers[i], NULL);
    }

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

    /* 3. Force a Garbage Collection to reap remaining zombies and purge RSS */
    //lzmalloc_gc();

    LZ_INFO("=== STRESS TEST SURVIVED ===");
    LZ_INFO("Total cross-thread pointers allocated and freed: %d", TOTAL_POINTERS);

    return 0;
}