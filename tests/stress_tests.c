/**
 * @file stress_tests.c
 * @brief High-Concurrency Cross-Thread Stress Test.
 * @details Implements a massive Producer-Consumer model to exercise the 
 * MPSC (Multi-Producer Single-Consumer) remote deallocation mailbox. 
 * This test is designed to expose race conditions in the RSEQ logic, 
 * mailbox atomic exchange, and the arena multiplexing try-locks.
 */

#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdatomic.h>
#include <sched.h>
#include "lz_log.h"
#include "lzmalloc.h" 

#define NUM_PRODUCERS 20
#define NUM_CONSUMERS 20
#define ALLOCS_PER_THREAD 10000
#define TOTAL_POINTERS (NUM_PRODUCERS * ALLOCS_PER_THREAD)

/** @brief Atomic shared buffer for cross-thread pointer passing. */
static _Atomic(void*) shared_pointers[TOTAL_POINTERS];
static _Atomic int write_idx = 0;
static _Atomic int read_idx = 0;

/**
 * @brief Producer Thread Routine.
 * @details Generates chaotic allocation sizes to hit all three engine tiers 
 * simultaneously, then publishes pointers to the shared buffer using 
 * Release semantics.
 */
static void* producer_worker(void* arg) {
    uintptr_t tid = (uintptr_t)arg;
    LZ_DEBUG("Producer %u online.", (unsigned)tid);

    for (int i = 0; i < ALLOCS_PER_THREAD; i++) {
        size_t size;
        int r = rand() % 100;
        
        /* Chaotic sizing adjusted to v0.2.0 boundaries */
        if (r < 85) {
            size = (rand() % 16384) + 8;       /* 85%: Slabs (8B - 16KB) */
        } else if (r < 98) {
            size = (rand() % 500000) + 35000;  /* 13%: Spans (35KB - 535KB) */
        } else {
            size = (rand() % 1048576) + 1100000; /* 2%: Direct Mmap (1.1MB - 2.1MB) */
        }

        void* ptr = malloc(size);
        
        if (LZ_LOG_UNLIKELY(!ptr)) {
            LZ_FATAL("OOM: Producer %u failed to allocate %zu bytes", (unsigned)tid, size);
            exit(EXIT_FAILURE);
        }

        /* Touch memory to ensure physical page faults are triggered */
        ((volatile char*)ptr)[0] = 'X';

        /* Publish the pointer for a consumer thread to find */
        int idx = atomic_fetch_add_explicit(&write_idx, 1, memory_order_relaxed);
        atomic_store_explicit(&shared_pointers[idx], ptr, memory_order_release);
    }
    return NULL;
}

/**
 * @brief Consumer Thread Routine.
 * @details Acquires pointers published by producers and deallocates them. 
 * Since the consumer core is likely different from the producer core, this 
 * triggers the asynchronous Remote Free path (MPSC Mailbox).
 */
static void* consumer_worker(void* arg) {
    uintptr_t tid = (uintptr_t)arg;
    LZ_DEBUG("Consumer %u online.", (unsigned)tid);

    int consumed = 0;
    int target_consumption = TOTAL_POINTERS / NUM_CONSUMERS;

    while (consumed < target_consumption) {
        int claim_idx = atomic_fetch_add_explicit(&read_idx, 1, memory_order_relaxed);
        
        if (claim_idx < TOTAL_POINTERS) {
            void* ptr_to_free = NULL;
            
            /* Acquire with barrier to sync with producer release */
            while ((ptr_to_free = atomic_load_explicit(&shared_pointers[claim_idx], memory_order_acquire)) == NULL) {
                sched_yield(); 
            }
            
            /* The core of the stress test: deallocating from a foreign thread/core */
            free(ptr_to_free); 
            consumed++;
        }
    }
    return NULL;
}

/**
 * @brief Entry point for the Concurrency Stress Test.
 */
int main(void) {
    LZ_INFO("=== CONCURRENCY STRESS TEST ===");
    LZ_INFO("Config: %d Producers, %d Consumers, %d Allocs/Thread", 
            NUM_PRODUCERS, NUM_CONSUMERS, ALLOCS_PER_THREAD);

    pthread_t producers[NUM_PRODUCERS];
    pthread_t consumers[NUM_CONSUMERS];

    /* Atomic initialization of the shared global buffer */
    for (int i = 0; i < TOTAL_POINTERS; i++) {
        atomic_init(&shared_pointers[i], NULL);
    }

    /* Spawn parallel workloads */
    for (uintptr_t i = 0; i < NUM_PRODUCERS; i++) {
        pthread_create(&producers[i], NULL, producer_worker, (void*)i);
    }
    for (uintptr_t i = 0; i < NUM_CONSUMERS; i++) {
        pthread_create(&consumers[i], NULL, consumer_worker, (void*)i);
    }

    /* Barrier: Wait for the workload to finish */
    for (int i = 0; i < NUM_PRODUCERS; i++) pthread_join(producers[i], NULL);
    for (int i = 0; i < NUM_CONSUMERS; i++) pthread_join(consumers[i], NULL);

    LZ_INFO("=== STRESS TEST SURVIVED ===");
    LZ_INFO("Total cross-thread pointers allocated and successfully freed: %d", TOTAL_POINTERS);

    return EXIT_SUCCESS;
}