/**
 * @file integration_tests.c
 * @brief POSIX compliance and API edge-case verification.
 * @details Ensures the allocator behaves exactly as specified by the C standard
 * and POSIX.
 */

#include <stdlib.h>
#include <stdint.h>
#include "lz_log.h"

#define ASSERT_TEST(cond, msg) \
    do { if (LZ_LOG_UNLIKELY(!(cond))) { LZ_FATAL("Integration Test Failed: %s", msg); } } while(0)

/**
 * @brief Tests strict memory alignment requirements requested by the user.
 */
void test_posix_memalign(void) {
    LZ_INFO("Running test_posix_memalign...");
    
    void* ptr = NULL;
    size_t alignment = 4096; /* 4KB Page alignment */
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
 * @brief Tests obscure but legal uses of realloc defined by the C standard.
 */
void test_realloc_edge_cases(void) {
    LZ_INFO("Running test_realloc_edge_cases...");
    
    /* 1. realloc with NULL behaves exactly like malloc */
    void* p1 = realloc(NULL, 64);
    ASSERT_TEST(p1 != NULL, "realloc(NULL, size) failed");

    /* 2. Write data to verify preservation during expansion */
    for (int i = 0; i < 64; i++) ((uint8_t*)p1)[i] = (uint8_t)i;

    /* 3. Expand memory (Forces internal memcpy and new block allocation) */
    void* p2 = realloc(p1, 2048);
    ASSERT_TEST(p2 != NULL, "realloc expansion failed");

    /* Verify data integrity */
    for (int i = 0; i < 64; i++) {
        ASSERT_TEST(((uint8_t*)p2)[i] == (uint8_t)i, "Data corrupted during realloc");
    }

    free(p2);
    LZ_INFO("test_realloc_edge_cases PASSED.");
}

int main(void) {
    LZ_INFO("=== STARTING INTEGRATION TESTS ===");
    test_posix_memalign();
    test_realloc_edge_cases();
    LZ_INFO("=== ALL INTEGRATION TESTS PASSED ===");
    return 0;
}