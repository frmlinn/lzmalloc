# ==============================================================================
# @file CMakeLists.txt
# @brief High-performance build configuration for lzmalloc
# @details This configuration manages the architecture by explicitly 
# mapping internal subdirectories to the compiler search path. It enforces 
# Link-Time Optimization (LTO) and hardware-specific tuning for maximum 
# mechanical sympathy.
# ==============================================================================

cmake_minimum_required(VERSION 3.12)
project(lzmalloc VERSION 2.0.0 LANGUAGES C)

# ------------------------------------------------------------------------------
# @section Build Profile & Optimization (LTO/IPO)
# ------------------------------------------------------------------------------
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported)
if(ipo_supported)
    message(STATUS "Mechanical Sympathy: LTO/IPO is ENABLED for Release builds.")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build options: Debug Release" FORCE)
endif()

# Propagate debug definitions to the config header
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(LZ_DEBUG_MODE 1)
endif()

# ------------------------------------------------------------------------------
# @section Hardware Topology & Architecture Detection
# ------------------------------------------------------------------------------
set(LZ_ARCH ${CMAKE_SYSTEM_PROCESSOR})
set(LZ_OS ${CMAKE_SYSTEM_NAME})

# Default Profile: Standard Linux x86_64
set(LZ_CACHE_LINE_SIZE 64 CACHE STRING "L1/L2 Cache line size in bytes")
set(LZ_PAGE_SIZE 4096 CACHE STRING "Base OS physical page size")
set(LZ_HUGE_PAGE_SIZE 2097152 CACHE STRING "Huge Page size (2MB)")
set(LZ_MAX_NUMA_NODES 16 CACHE STRING "Max supported NUMA domains")
set(LZ_MAX_SLAB_OBJ_SIZE 32768 CACHE STRING "Max size class for Slab Engine (32KB)")
set(LZ_VMM_MAX_CACHED_CHUNKS 32 CACHE STRING "Max idle chunks per NUMA pool")
set(LZ_CHUNK_SHIFT 21 CACHE STRING "Bit shift for 2MB Chunk resolution")
set(LZ_BATCH_MAX_SIZE 64 CACHE STRING "Max batch size for remote frees")
set(LZ_MAX_TELEMETRY_SLOTS 4096 CACHE STRING "Total SHM telemetry matrix slots")

# Architecture-specific manual tuning
if(LZ_ARCH MATCHES "^(aarch64|arm64)")
    if(LZ_OS STREQUAL "Darwin")
        set(LZ_CACHE_LINE_SIZE 128)
        set(LZ_PAGE_SIZE 16384)       
        set(LZ_HUGE_PAGE_SIZE 33554432) 
        set(LZ_CHUNK_SHIFT 25)
        message(STATUS "Topology Profile: Apple Silicon (M-Series) detected.")
    endif()
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/lz_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/lz_config.h
    @ONLY
)

# ------------------------------------------------------------------------------
# @section Source Tree Mapping
# ------------------------------------------------------------------------------


set(LZ_INTERNAL_SRCS src/internal/topology.c src/internal/telemetry.c)
set(LZ_CORE_SRCS src/core/rtree.c src/core/security.c)
set(LZ_ENGINES_SRCS src/engines/slab.c src/engines/span.c src/engines/vmm.c)
set(LZ_TLH_SRCS src/tlh/tlh.c)
set(LZ_HOOKS_SRCS src/hooks/lzmalloc.c src/hooks/posix_hooks.c)

add_subdirectory(external/lz-logger)

add_library(lzmalloc SHARED 
    ${LZ_INTERNAL_SRCS} 
    ${LZ_CORE_SRCS} 
    ${LZ_ENGINES_SRCS} 
    ${LZ_TLH_SRCS} 
    ${LZ_HOOKS_SRCS}
)

# ------------------------------------------------------------------------------
# @section Compiler Flags & Security Hardening
# ------------------------------------------------------------------------------
target_compile_options(lzmalloc PRIVATE
    -Wall -Wextra -Werror -std=c11
    -fvisibility=hidden             # Protect internal symbols
    -ftls-model=initial-exec        # Fastest TLS access for allocators
    # @note Explicitly disable built-ins to prevent infinite recursion during bootstrap
    -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free
    $<$<CONFIG:Release>:-O3 -march=native -mtune=native -fno-omit-frame-pointer>
    $<$<CONFIG:Debug>:-O0 -g3 -fno-omit-frame-pointer>
)

target_link_options(lzmalloc PRIVATE
    -Wl,-Bsymbolic -Wl,-z,now -Wl,-z,relro
)

# ------------------------------------------------------------------------------
# @section Include Path Resolution (Fixing common.h visibility)
# ------------------------------------------------------------------------------
target_include_directories(lzmalloc 
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include        # Public API
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core       # Layer 4
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/engines    # Layer 5
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/internal   # Layers 2-3
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/tlh        # Layer 6
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/hooks      # Layer 7
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}                # lz_config.h
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external/lz-logger/include
)

target_link_libraries(lzmalloc PRIVATE pthread lz_logger)

# ------------------------------------------------------------------------------
# @section Test Integration (LD_PRELOAD enabled)
# ------------------------------------------------------------------------------
enable_testing()

macro(add_lz_test TEST_NAME SRC_FILE)
    add_executable(${TEST_NAME} ${SRC_FILE})
    target_include_directories(${TEST_NAME} PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/include 
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/internal       # For common headers in tests
        ${CMAKE_CURRENT_SOURCE_DIR}/external/lz-logger/include
    )
    
    target_link_libraries(${TEST_NAME} PRIVATE lzmalloc lz_logger pthread)
    
    # Register test and force the use of our shared library
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_property(TEST ${TEST_NAME} PROPERTY ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:lzmalloc>")
endmacro()

add_lz_test(unit_test tests/unit_tests.c)
add_lz_test(integration_test tests/integration_tests.c)
add_lz_test(stress_test tests/stress_tests.c)