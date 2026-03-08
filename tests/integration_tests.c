/**
 * @file integration_tests.c
 * @brief POSIX compliance and cross-engine migration verification.
 * @details Ensures the allocator behaves exactly as specified by the C standard,
 * specifically testing edge cases like engine boundary crossing during realloc.
 */

#include <stdlib.h>
#include <stdint.h>
#include "lz_log.h"

#define ASSERT_TEST(cond, msg) \
    do { if (LZ_LOG_UNLIKELY(!(cond))) { LZ_FATAL("Integration Test Failed: %s", msg); } } while(0)

/**
 * @brief Tests strict memory alignment requirements.
 */
void test_posix_memalign(void) {
    LZ_INFO("Running test_posix_memalign...");
    
    void* ptr = NULL;
    size_t alignment = 4096; /* OS Page alignment */
    size_t size = 1024;

    int res = posix_memalign(&ptr, alignment, size);
    ASSERT_TEST(res == 0, "posix_memalign returned non-zero error code");
    ASSERT_TEST(ptr != NULL, "posix_memalign returned NULL pointer");
    
    /* Verify mathematical alignment: (ptr % alignment) == 0 */
    ASSERT_TEST(((uintptr_t)ptr & (alignment - 1)) == 0, "Pointer is not strictly aligned");
    
    free(ptr);
    LZ_INFO("test_posix_memalign PASSED.");
}

/**
 * @brief Tests engine migration via realloc (Slab -> Span -> Direct).
 */
void test_realloc_migrations(void) {
    LZ_INFO("Running test_realloc_migrations...");
    
    /* 1. Allocate in Slab (16 KB) */
    void* p1 = realloc(NULL, 16 * 1024);
    ASSERT_TEST(p1 != NULL, "realloc(NULL) to Slab failed");
    for (int i = 0; i < 16 * 1024; i++) ((uint8_t*)p1)[i] = (uint8_t)(i % 256);

    /* 2. Migrate to Span (256 KB) */
    void* p2 = realloc(p1, 256 * 1024);
    ASSERT_TEST(p2 != NULL, "Migration Slab -> Span failed");
    for (int i = 0; i < 16 * 1024; i++) {
        ASSERT_TEST(((uint8_t*)p2)[i] == (uint8_t)(i % 256), "Data corrupted during Slab->Span migration");
    }
    for (int i = 16 * 1024; i < 256 * 1024; i++) ((uint8_t*)p2)[i] = 0xAA;

    /* 3. Migrate to Direct Mmap (2 MB) */
    void* p3 = realloc(p2, 2 * 1024 * 1024);
    ASSERT_TEST(p3 != NULL, "Migration Span -> Direct failed");
    for (int i = 16 * 1024; i < 256 * 1024; i++) {
        ASSERT_TEST(((uint8_t*)p3)[i] == 0xAA, "Data corrupted during Span->Direct migration");
    }

    free(p3);
    LZ_INFO("test_realloc_migrations PASSED.");
}

int main(void) {
    LZ_INFO("=== STARTING INTEGRATION TESTS ===");
    test_posix_memalign();
    test_realloc_migrations();
    LZ_INFO("=== ALL INTEGRATION TESTS PASSED ===");
    return 0;
}