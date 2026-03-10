/**
 * @file bench_suite.c
 * @brief Benchmarking suite for memory allocators.
 * Compares Latency, Throughput (Producer-Consumer), and VMA Fragmentation.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>
#include <stdatomic.h>
#include <dlfcn.h>

/* ========================================================================= *
 * Configuration & Tunables
 * ========================================================================= */
#define NUM_THREADS 16
#define OPS_PER_THREAD 1000000
#define BATCH_SIZE 256
#define ALLOC_SIZE_SMALL 64

/* Fragmentation Test Settings (Targeting Span Engine: 32KB - 1MB) */
#define CHAOS_ALLOC_COUNT 20000
#define MIN_SPAN_SIZE (32 * 1024)
#define MAX_SPAN_SIZE (1024 * 1024)

/* ========================================================================= *
 * Utilities
 * ========================================================================= */
static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/**
 * @brief Gets the current Resident Set Size (RSS) in KB from Linux procfs.
 * @details This is the ONLY way to see memory deflation after MADV_DONTNEED.
 */
static long get_current_rss_kb(void) {
    FILE* f = fopen("/proc/self/statm", "r");
    if (!f) return 0;

    long size = 0, resident = 0;
    if (fscanf(f, "%ld %ld", &size, &resident) != 2) {
        resident = 0;
    }
    fclose(f);

    long page_size = sysconf(_SC_PAGESIZE);
    return (resident * page_size) / 1024;
}

/**
 * @brief Prints both Current and Peak RSS. 
 * @note Safe to call as long as it is strictly OUTSIDE timed blocks.
 */
static void print_rss(const char* label) {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    long current_rss = get_current_rss_kb();
    
    printf("[RSS] %-30s : %ld KB (Peak: %ld KB)\n", label, current_rss, usage.ru_maxrss);
}

/* PRNG based on xorshift32 for fast deterministic chaos */
static uint32_t fast_rand(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* ========================================================================= *
 * Benchmark 1: Pure Latency (Single Threaded)
 * ========================================================================= */
void bench_latency(void) {
    void** ptrs = malloc(sizeof(void*) * OPS_PER_THREAD);
    if (!ptrs) { printf("OOM in bench_latency\n"); return; }
    
    /* Warm-up Phase */
    for (int i = 0; i < 10000; i++) { free(malloc(ALLOC_SIZE_SMALL)); }

    /* TIMED BLOCK: Alloc */
    uint64_t start_alloc = get_time_ns();
    for (int i = 0; i < OPS_PER_THREAD; i++) {
        ptrs[i] = malloc(ALLOC_SIZE_SMALL);
    }
    uint64_t end_alloc = get_time_ns();

    /* TIMED BLOCK: Free */
    uint64_t start_free = get_time_ns();
    for (int i = 0; i < OPS_PER_THREAD; i++) {
        free(ptrs[i]);
    }
    uint64_t end_free = get_time_ns();

    /* I/O is safely outside the timers */
    printf("\n--- Benchmark 1: Pure Latency (Single Thread, %d ops) ---\n", OPS_PER_THREAD);
    printf("Alloc Latency : %.2f ns/op\n", (double)(end_alloc - start_alloc) / OPS_PER_THREAD);
    printf("Free Latency  : %.2f ns/op\n", (double)(end_free - start_free) / OPS_PER_THREAD);
    
    free(ptrs);
    print_rss("After Latency Bench");
}

/* ========================================================================= *
 * Benchmark 2: Producer-Consumer (Throughput & Batching)
 * ========================================================================= */
typedef struct {
    void* pointers[BATCH_SIZE];
    _Atomic uint32_t ready;
} exchange_buffer_t;

exchange_buffer_t g_buffers[NUM_THREADS];
atomic_flag g_start_flag = ATOMIC_FLAG_INIT;

void* producer_thread(void* arg) {
    int tid = (int)(intptr_t)arg;
    int target_tid = (tid + 1) % NUM_THREADS; 
    
    while (atomic_flag_test_and_set_explicit(&g_start_flag, memory_order_acquire)) {}
    atomic_flag_clear_explicit(&g_start_flag, memory_order_release);

    for (int i = 0; i < OPS_PER_THREAD / BATCH_SIZE; i++) {
        while (atomic_load_explicit(&g_buffers[target_tid].ready, memory_order_acquire) == 1) {}
        
        for (int j = 0; j < BATCH_SIZE; j++) {
            g_buffers[target_tid].pointers[j] = malloc(ALLOC_SIZE_SMALL);
        }
        atomic_store_explicit(&g_buffers[target_tid].ready, 1, memory_order_release);
    }
    return NULL;
}

void* consumer_thread(void* arg) {
    int tid = (int)(intptr_t)arg;

    while (atomic_flag_test_and_set_explicit(&g_start_flag, memory_order_acquire)) {}
    atomic_flag_clear_explicit(&g_start_flag, memory_order_release);

    for (int i = 0; i < OPS_PER_THREAD / BATCH_SIZE; i++) {
        while (atomic_load_explicit(&g_buffers[tid].ready, memory_order_acquire) == 0) {}
        
        for (int j = 0; j < BATCH_SIZE; j++) {
            free(g_buffers[tid].pointers[j]);
        }
        atomic_store_explicit(&g_buffers[tid].ready, 0, memory_order_release);
    }
    return NULL;
}

void bench_throughput(void) {
    pthread_t prods[NUM_THREADS], cons[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        atomic_init(&g_buffers[i].ready, 0);
    }

    atomic_flag_test_and_set(&g_start_flag);

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&cons[i], NULL, consumer_thread, (void*)(intptr_t)i);
        pthread_create(&prods[i], NULL, producer_thread, (void*)(intptr_t)i);
    }

    /* TIMED BLOCK: Thread Execution */
    uint64_t start_time = get_time_ns();
    atomic_flag_clear(&g_start_flag);

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(prods[i], NULL);
        pthread_join(cons[i], NULL);
    }
    uint64_t end_time = get_time_ns();

    /* I/O is safely outside the timers */
    printf("\n--- Benchmark 2: Producer-Consumer Throughput (%d Threads) ---\n", NUM_THREADS * 2);
    double total_sec = (double)(end_time - start_time) / 1e9;
    uint64_t total_ops = NUM_THREADS * OPS_PER_THREAD;
    printf("Total Time    : %.3f sec\n", total_sec);
    printf("Throughput    : %.2f Million Ops/sec\n", (total_ops / total_sec) / 1e6);
    
    print_rss("After Throughput Bench");
}

/* ========================================================================= *
 * Benchmark 3: Chaos Fragmentation (Medium Objects & VMM Stress)
 * ========================================================================= */
void bench_fragmentation(void) {
    void** ptrs = malloc(sizeof(void*) * CHAOS_ALLOC_COUNT);
    uint32_t rng_state = 42; 

    uint64_t total_requested_bytes = 0;

    /* TIMED BLOCK: Chaos Alloc */
    uint64_t start_alloc = get_time_ns();
    for (int i = 0; i < CHAOS_ALLOC_COUNT; i++) {
        size_t size = MIN_SPAN_SIZE + (fast_rand(&rng_state) % (MAX_SPAN_SIZE - MIN_SPAN_SIZE));
        ptrs[i] = malloc(size);
        total_requested_bytes += size;
        
        if (ptrs[i]) { memset(ptrs[i], 0xAA, size); }
    }
    uint64_t end_alloc = get_time_ns();

    /* I/O safely outside */
    printf("\n--- Benchmark 3: Chaos Fragmentation (%d Med-Objects) ---\n", CHAOS_ALLOC_COUNT);
    printf("Total Payload : %.2f MB\n", (double)total_requested_bytes / (1024 * 1024));
    print_rss("Peak State (Before Free)");

    /* Non-timed shuffle */
    for (int i = CHAOS_ALLOC_COUNT - 1; i > 0; i--) {
        int j = fast_rand(&rng_state) % (i + 1);
        void* temp = ptrs[i];
        ptrs[i] = ptrs[j];
        ptrs[j] = temp;
    }

    /* TIMED BLOCK: Chaos Free */
    uint64_t start_free = get_time_ns();
    for (int i = 0; i < CHAOS_ALLOC_COUNT; i++) {
        free(ptrs[i]);
    }
    uint64_t end_free = get_time_ns();

    /* I/O safely outside */
    printf("Alloc Time    : %.3f sec\n", (double)(end_alloc - start_alloc) / 1e9);
    printf("Free Time     : %.3f sec\n", (double)(end_free - start_free) / 1e9);

    free(ptrs);
    print_rss("After Chaos Bench");
}

/* ========================================================================= *
 * Main Execution
 * ========================================================================= */
int main(void) {
    printf("==========================================\n");
    printf(" Allocator Benchmarking Suite v2.1\n");
    printf("==========================================\n");
    
    print_rss("Baseline Start");
    
    bench_latency();
    bench_throughput();
    bench_fragmentation();

    printf("\n--- Teardown Phase ---\n");
    void (*gc_func)(void) = dlsym(RTLD_DEFAULT, "lzmalloc_gc");
    if (gc_func) {
        printf("lzmalloc_gc() detected. Forcing background reap...\n");
        gc_func();
        print_rss("Final State (After GC)");
    } else {
        print_rss("Final State");
    }

    printf("==========================================\n");
    return 0;
}