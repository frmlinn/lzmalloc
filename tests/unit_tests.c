/**
 * @file unit_tests.c
 * @brief Core sanity checks for the triple-hierarchy allocation primitives.
 * @details Validates the primary fast-paths (Slabs), medium-paths (Spans), 
 * and slow-paths (Direct Mmap) of the lzmalloc V2 engine using standard POSIX hooks.
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "lz_log.h"

/**
 * @brief Macro for clean assertions using the async-signal-safe logger.
 */
#define ASSERT_TEST(cond, msg) \
    do { if (LZ_LOG_UNLIKELY(!(cond))) { LZ_FATAL("Unit Test Failed: %s", msg); exit(EXIT_FAILURE); } } while(0)

/**
 * @brief Tests allocation and deallocation across all three memory engines.
 */
static void test_basic_alloc_free(void) {
    LZ_INFO("Running test_basic_alloc_free (Triple Hierarchy Validation)...");
    
    /* 1. Small allocation (128 Bytes): Served by the Slab Engine */
    void* p_slab = malloc(128);
    ASSERT_TEST(p_slab != NULL, "Slab allocation (128B) failed");
    memset(p_slab, 0xAA, 128); 
    free(p_slab);

    /* 2. Medium allocation (256 KB): Served by the Span Engine */
    size_t span_size = 256 * 1024;
    void* p_span = malloc(span_size);
    ASSERT_TEST(p_span != NULL, "Span allocation (256KB) failed");
    memset(p_span, 0xBB, span_size);
    free(p_span);

    /* 3. Huge allocation (3 MB): Served by Direct Mmap Engine */
    size_t huge_size = 3 * 1024 * 1024; 
    void* p_direct = malloc(huge_size);
    ASSERT_TEST(p_direct != NULL, "Direct allocation (3MB) failed");
    memset(p_direct, 0xCC, huge_size);
    free(p_direct);

    LZ_INFO("test_basic_alloc_free PASSED.");
}

/**
 * @brief Ensures calloc strictly zeroes out the returned memory block.
 */
static void test_calloc_zeroing(void) {
    LZ_INFO("Running test_calloc_zeroing...");
    
    size_t num = 128;
    size_t size = 16; /* 2048 bytes total -> Slab Engine routing */
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
    return EXIT_SUCCESS;
}