# ==============================================================================
# @file CMakeLists.txt
# @brief High-performance Build System for lzmalloc v0.2.0.
# @details Configures Link-Time Optimization (LTO), hardware topology detection, 
# and compiler-level "mechanical sympathy" flags for Linux/Darwin environments.
# ==============================================================================
cmake_minimum_required(VERSION 3.12)
project(lzmalloc VERSION 0.2.0 LANGUAGES C)

include(CheckIPOSupported)
include(CheckIncludeFile)
include(GNUInstallDirs)

# ------------------------------------------------------------------------------
# 1. Compiler Optimization (LTO/IPO)
# ------------------------------------------------------------------------------
check_ipo_supported(RESULT ipo_supported)
if(ipo_supported)
    # Enable Interprocedural Optimization to allow inlining across translation units
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# ------------------------------------------------------------------------------
# 2. Silicon Topology & OS Detection
# ------------------------------------------------------------------------------
set(LZ_ARCH ${CMAKE_SYSTEM_PROCESSOR})
set(LZ_OS ${CMAKE_SYSTEM_NAME})

# Base profiles for standard Linux x86_64
set(LZ_CACHE_LINE_SIZE 64)
set(LZ_PAGE_SIZE 4096)
set(LZ_HUGE_PAGE_SIZE 2097152) 
set(LZ_CHUNK_SHIFT 21)
set(LZ_MAX_CORES 128)

# Apple Silicon (M-Series) Overrides
if(LZ_ARCH MATCHES "^(aarch64|arm64)" AND LZ_OS STREQUAL "Darwin")
    set(LZ_CACHE_LINE_SIZE 128)
    set(LZ_PAGE_SIZE 16384)       
    set(LZ_HUGE_PAGE_SIZE 33554432) 
    set(LZ_CHUNK_SHIFT 25)
endif()

# ------------------------------------------------------------------------------
# 3. Kernel Capability Detection (Restartable Sequences)
# ------------------------------------------------------------------------------
check_include_file("linux/rseq.h" HAS_RSEQ_HEADER)
if(HAS_RSEQ_HEADER OR LZ_OS STREQUAL "Linux")
    set(LZ_HAS_RSEQ 1)
endif()

# ------------------------------------------------------------------------------
# 4. Mathematical Contract Injection
# ------------------------------------------------------------------------------
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/lz_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/lz_config.h
    @ONLY
)

# ------------------------------------------------------------------------------
# 5. External Integration (Logging Subsystem)
# ------------------------------------------------------------------------------
add_subdirectory(external/lz-logger)

# ------------------------------------------------------------------------------
# 6. Source Tree Organization (Layered Architecture)
# ------------------------------------------------------------------------------
set(LZ_LAYER0 src/arch/cpu_rt.c)
set(LZ_LAYER1 src/os/memory.c src/os/security.c)
set(LZ_LAYER2 src/meta/pagemap.c)
set(LZ_LAYER3 
    src/engines/vmm.c 
    src/engines/core_heap.c 
    src/engines/span_bin.c 
    src/engines/slab.c
)
set(LZ_LAYER4 src/api/api.c src/api/posix.c)

# ------------------------------------------------------------------------------
# 7. Library Target Definition
# ------------------------------------------------------------------------------
add_library(lzmalloc SHARED ${LZ_LAYER0} ${LZ_LAYER1} ${LZ_LAYER2} ${LZ_LAYER3} ${LZ_LAYER4})

# ------------------------------------------------------------------------------
# 8. Mechanical Sympathy Flags
# ------------------------------------------------------------------------------
target_compile_options(lzmalloc PRIVATE
    -Wall -Wextra -Werror -std=c11
    -fvisibility=hidden
    # Avoid recursion: The allocator must not call its own hooks during init
    -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free
    $<$<CONFIG:Release>:-O3 -march=native -mtune=native -fno-omit-frame-pointer>
    $<$<CONFIG:Debug>:-O0 -g3>
)

target_link_options(lzmalloc PRIVATE -Wl,-Bsymbolic -Wl,-z,now -Wl,-z,relro)

find_package(Threads REQUIRED)
target_link_libraries(lzmalloc PRIVATE Threads::Threads lz_logger)

target_include_directories(lzmalloc PRIVATE 
    ${CMAKE_CURRENT_BINARY_DIR} 
    src/arch src/os src/meta src/engines src/api include
    external/lz-logger/include
)

# ------------------------------------------------------------------------------
# 9. Test Suite (LD_PRELOAD Logic)
# ------------------------------------------------------------------------------
enable_testing()

macro(add_lz_test TEST_NAME SRC_FILE)
    add_executable(${TEST_NAME} ${SRC_FILE})
    target_include_directories(${TEST_NAME} PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/include 
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/external/lz-logger/include
    )
    target_link_libraries(${TEST_NAME} PRIVATE lzmalloc lz_logger pthread)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    # Inject lzmalloc via LD_PRELOAD to intercept standard libc allocations
    set_property(TEST ${TEST_NAME} PROPERTY ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:lzmalloc>")
endmacro()

add_lz_test(unit_test tests/unit_tests.c)
add_lz_test(integration_test tests/integration_tests.c)
add_lz_test(stress_test tests/stress_tests.c)

# ------------------------------------------------------------------------------
# 10. Installation Rules
# ------------------------------------------------------------------------------
install(TARGETS lzmalloc
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/lzmalloc.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/lzmalloc
)