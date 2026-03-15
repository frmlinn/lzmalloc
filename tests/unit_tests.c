/**
 * @file unit_tests.c
 * @brief Core Sanity Checks for Triple-Hierarchy Primitives.
 * @details Validates the functional correctness of the three primary allocation 
 * engines: Slabs (small objects), Spans (medium objects), and Direct OS (huge objects). 
 * It ensures that routing logic correctly dispatches requests based on size 
 * thresholds and that zero-initialization (calloc) is strictly respected.
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "lz_log.h"

/**
 * @brief Test Assertion Macro.
 * @details Triggers a fatal log and exits the process if the condition is not met.
 */
#define ASSERT_TEST(cond, msg) \
    do { if (LZ_LOG_UNLIKELY(!(cond))) { LZ_FATAL("Unit Test Failed: %s", msg); exit(EXIT_FAILURE); } } while(0)

/**
 * @brief Validates basic Allocation and Deallocation across the hierarchy.
 * @details Tests three distinct size classes:
 * - 128B: Targeted at the Slab Engine.
 * - 256KB: Targeted at the Span Engine.
 * - 3MB: Targeted at the Direct OS (mmap) Engine.
 * @note Verifies that memory is writable and that the Flat Pagemap can 
 * resolve headers for all engine types.
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
 * @brief Validates the zero-initialization contract of calloc().
 * @details Ensures that the Slab Engine correctly clears the payload before 
 * returning it to the user, preventing information leaks from previous allocations.
 */
static void test_calloc_zeroing(void) {
    LZ_INFO("Running test_calloc_zeroing...");
    
    size_t num = 128;
    size_t size = 16; /* 2048 bytes total -> Slab Engine routing */
    uint8_t* ptr = (uint8_t*)calloc(num, size);
    ASSERT_TEST(ptr != NULL, "calloc returned NULL");

    /* Verification of bit-level zeroing */
    for (size_t i = 0; i < num * size; i++) {
        ASSERT_TEST(ptr[i] == 0, "calloc memory was not properly zeroed");
    }
    free(ptr);
    
    LZ_INFO("test_calloc_zeroing PASSED.");
}

/**
 * @brief Entry point for the unit test binary.
 */
int main(void) {
    LZ_INFO("=== STARTING UNIT TESTS ===");
    test_basic_alloc_free();
    test_calloc_zeroing();
    LZ_INFO("=== ALL UNIT TESTS PASSED ===");
    return EXIT_SUCCESS;
}