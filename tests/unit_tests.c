/**
 * @file unit_tests.c
 * @brief Core sanity checks for basic allocation primitives.
 * @details Validates the primary fast-paths (malloc, free) and memory 
 * initialization guarantees (calloc) of the lzmalloc engine.
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "lz_log.h"

/**
 * @brief Macro for clean assertions that use our custom async-signal-safe logger.
 */
#define ASSERT_TEST(cond, msg) \
    do { if (LZ_LOG_UNLIKELY(!(cond))) { LZ_FATAL("Unit Test Failed: %s", msg); } } while(0)

/**
 * @brief Tests basic Slab allocation (small sizes) and VMM direct allocation (huge sizes).
 */
void test_basic_alloc_free(void) {
    LZ_INFO("Running test_basic_alloc_free...");
    
    /* Small allocation: should be served by the Slab Allocator */
    void* p1 = malloc(128);
    ASSERT_TEST(p1 != NULL, "malloc(128) returned NULL");
    memset(p1, 0xAA, 128); /* Prove memory is writable */
    free(p1);

    /* Huge allocation: 3MB bypasses Slabs and hits the VMM / Huge Pages */
    size_t huge_size = 3 * 1024 * 1024; 
    void* p2 = malloc(huge_size);
    ASSERT_TEST(p2 != NULL, "malloc(3MB) returned NULL");
    memset(p2, 0xBB, huge_size);
    free(p2);

    LZ_INFO("test_basic_alloc_free PASSED.");
}

/**
 * @brief Ensures calloc strictly zeroes out the returned memory block.
 */
void test_calloc_zeroing(void) {
    LZ_INFO("Running test_calloc_zeroing...");
    
    size_t num = 128;
    size_t size = 16;
    uint8_t* ptr = (uint8_t*)calloc(num, size);
    ASSERT_TEST(ptr != NULL, "calloc returned NULL");

    for (size_t i = 0; i < num * size; i++) {
        ASSERT_TEST(ptr[i] == 0, "calloc memory was not properly zeroed");
    }
    free(ptr);
    
    LZ_INFO("test_calloc_zeroing PASSED.");
}

int main(void) {
    LZ_INFO("=== STARTING UNIT TESTS ===");
    test_basic_alloc_free();
    test_calloc_zeroing();
    LZ_INFO("=== ALL UNIT TESTS PASSED ===");
    return 0;
}