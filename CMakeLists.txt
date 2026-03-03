cmake_minimum_required(VERSION 3.10)
project(lzmalloc VERSION 1.0.0 LANGUAGES C)

# ==============================================================================
# 1. Global Optimization Settings (LTO / IPO)
# ==============================================================================
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported)
if(ipo_supported)
    message(STATUS "Link-Time Optimization (LTO/IPO) is SUPPORTED and ENABLED for Release.")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
    message(WARNING "Link-Time Optimization (LTO/IPO) is NOT supported by this toolchain.")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build options: Debug Release" FORCE)
endif()

# ==============================================================================
# 2. Architecture Detection and Hardware Configuration
# ==============================================================================
set(LZ_ARCH ${CMAKE_SYSTEM_PROCESSOR})
set(LZ_OS ${CMAKE_SYSTEM_NAME})

# Default values (Standard Linux x86_64)
set(LZ_CACHE_LINE_SIZE 64 CACHE STRING "Cache line size in bytes")
set(LZ_PAGE_SIZE 4096 CACHE STRING "Base OS page size in bytes")
set(LZ_HUGE_PAGE_SIZE 2097152 CACHE STRING "Huge Page size in bytes (2MB)")
set(LZ_MAX_NUMA_NODES 16 CACHE STRING "Maximum supported NUMA nodes")
set(LZ_MAX_SLAB_OBJ_SIZE 32768 CACHE STRING "Maximum size in bytes for the Slab engine (32KB)")
set(LZ_VMM_MAX_CACHED_CHUNKS 32 CACHE STRING "Max cached 2MB chunks per NUMA node")
set(LZ_CHUNK_SHIFT 21 CACHE STRING "Bit shift for Chunk calculation (21 = 2MB)")
set(LZ_BATCH_MAX_SIZE 64 CACHE STRING "Maximum objects in remote free batch")
set(LZ_MAX_TELEMETRY_SLOTS 4096 CACHE STRING "Total slots in the SHM telemetry matrix")
set(LZ_LSM_SLOTS_COUNT 16 CACHE STRING "Number of telemetry shards for LSM regions")

# Dentro del if de Apple Silicon:
 # 32MB Huge pages requieren un shift de 25 bits

# Architecture-specific tuning
if(LZ_ARCH MATCHES "^(aarch64|arm64)")
    if(LZ_OS STREQUAL "Darwin")
        # Apple Silicon (M1/M2/M3)
        set(LZ_CACHE_LINE_SIZE 128)
        set(LZ_PAGE_SIZE 16384)       # 16KB Pages
        set(LZ_HUGE_PAGE_SIZE 33554432) # 32MB Huge pages on macOS
        set(LZ_CHUNK_SHIFT 25)
        message(STATUS "Hardware Profile: Apple Silicon detected.")
    else()
        # Generic ARM64 Linux
        message(STATUS "Hardware Profile: Generic ARM64 detected.")
    endif()
elseif(LZ_ARCH MATCHES "^(x86_64|AMD64)")
    message(STATUS "Hardware Profile: Standard x86_64 detected.")
endif()

# Generate lz_config.h in the build directory
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/lz_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/lz_config.h
    @ONLY
)

add_subdirectory(external/lz-logger)

# Collect all source files
file(GLOB LIB_SRCS "src/*.c")

# Library Target
add_library(lzmalloc SHARED ${LIB_SRCS})

target_compile_options(lzmalloc PRIVATE
    -Wall -Wextra 
    -fvisibility=hidden 
    -ftls-model=initial-exec
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Debug>:-O0 -g>
)

target_link_options(lzmalloc PRIVATE
    -Wl,-Bsymbolic -Wl,-z,now -Wl,-z,relro
)

target_include_directories(lzmalloc PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include 
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/external/lz-logger/include
)

target_link_libraries(lzmalloc PRIVATE pthread lz_logger)

# ==============================================================================
# 5. Tests Suite
# ==============================================================================
enable_testing()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/lz-logger/include)

add_executable(unit_test tests/unit_tests.c)
target_link_libraries(unit_test PRIVATE lz_logger)

add_executable(integration_test tests/integration_tests.c)
target_link_libraries(integration_test PRIVATE lz_logger)

add_executable(stress_test tests/stress_tests.c)
target_link_libraries(stress_test PRIVATE lz_logger pthread)