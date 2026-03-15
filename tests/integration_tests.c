/**
 * @file integration_tests.c
 * @brief POSIX Compliance and Cross-Engine Migration Verification.
 * @details Tests the allocator's ability to handle complex memory life-cycles, 
 * including strict alignment requirements (posix_memalign) and data integrity 
 * during realloc() calls that trigger migrations between different engines 
 * (e.g., Slab to Span).
 */

#include <stdlib.h>
#include <stdint.h>
#include "lz_log.h"

#define ASSERT_TEST(cond, msg) \
    do { if (LZ_LOG_UNLIKELY(!(cond))) { LZ_FATAL("Integration Test Failed: %s", msg); exit(EXIT_FAILURE); } } while(0)

/**
 * @brief Verifies strict mathematical alignment of pointers.
 * @details Checks if posix_memalign() respects power-of-2 boundaries and 
 * correctly bypasses internal slab metadata to provide a raw page-aligned pointer.
 */
static void test_posix_memalign(void) {
    LZ_INFO("Running test_posix_memalign...");
    
    void* ptr = NULL;
    size_t alignment = 4096; /* OS Page alignment */
    size_t size = 1024;

    int res = posix_memalign(&ptr, alignment, size);
    ASSERT_TEST(res == 0, "posix_memalign returned non-zero error code");
    ASSERT_TEST(ptr != NULL, "posix_memalign returned NULL pointer");
    
    /* Bitwise verify mathematical alignment */
    ASSERT_TEST(((uintptr_t)ptr & (alignment - 1)) == 0, "Pointer is not strictly aligned to 4096 bytes");
    
    free(ptr);
    LZ_INFO("test_posix_memalign PASSED.");
}

/**
 * @brief Validates Data Integrity during Engine Migration.
 * @details Ensures that data is correctly preserved when realloc() triggers 
 * an upgrade in the engine hierarchy:
 * 1. Slab (16KB) -> 2. Span (256KB) -> 3. Direct (2MB).
 */
static void test_realloc_migrations(void) {
    LZ_INFO("Running test_realloc_migrations...");
    
    /* 1. Allocate in Slab Engine (16 KB) */
    void* p1 = realloc(NULL, 16 * 1024);
    ASSERT_TEST(p1 != NULL, "realloc(NULL) to Slab failed");
    for (int i = 0; i < 16 * 1024; i++) ((uint8_t*)p1)[i] = (uint8_t)(i % 256);

    /* 2. Migrate to Span Engine (256 KB) */
    void* p2 = realloc(p1, 256 * 1024);
    ASSERT_TEST(p2 != NULL, "Migration Slab -> Span failed");
    
    /* Verify data persisted after migration */
    for (int i = 0; i < 16 * 1024; i++) {
        ASSERT_TEST(((uint8_t*)p2)[i] == (uint8_t)(i % 256), "Data corrupted during Slab->Span migration");
    }
    for (int i = 16 * 1024; i < 256 * 1024; i++) ((uint8_t*)p2)[i] = 0xAA;

    /* 3. Migrate to Direct Mmap Engine (2 MB) */
    void* p3 = realloc(p2, 2 * 1024 * 1024);
    ASSERT_TEST(p3 != NULL, "Migration Span -> Direct failed");
    
    for (int i = 16 * 1024; i < 256 * 1024; i++) {
        ASSERT_TEST(((uint8_t*)p3)[i] == 0xAA, "Data corrupted during Span->Direct migration");
    }

    free(p3);
    LZ_INFO("test_realloc_migrations PASSED.");
}

/**
 * @brief Entry point for the integration test binary.
 */
int main(void) {
    LZ_INFO("=== STARTING INTEGRATION TESTS ===");
    test_posix_memalign();
    test_realloc_migrations();
    LZ_INFO("=== ALL INTEGRATION TESTS PASSED ===");
    return EXIT_SUCCESS;
}