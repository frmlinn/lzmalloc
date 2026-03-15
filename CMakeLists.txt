# ==============================================================================
# lzmalloc v0.2.0 - Core Build System
# ==============================================================================
cmake_minimum_required(VERSION 3.12)
project(lzmalloc VERSION 0.2.0 LANGUAGES C)

include(CheckIPOSupported)
include(CheckIncludeFile)
include(GNUInstallDirs)

# 1. Optimizaciones de Compilador (LTO)
check_ipo_supported(RESULT ipo_supported)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# 2. Detección de Topología de Silicio y OS
set(LZ_ARCH ${CMAKE_SYSTEM_PROCESSOR})
set(LZ_OS ${CMAKE_SYSTEM_NAME})

# Valores base (Linux x86_64)
set(LZ_CACHE_LINE_SIZE 64)
set(LZ_PAGE_SIZE 4096)
set(LZ_HUGE_PAGE_SIZE 2097152) 
set(LZ_CHUNK_SHIFT 21)
set(LZ_MAX_CORES 128)

if(LZ_ARCH MATCHES "^(aarch64|arm64)" AND LZ_OS STREQUAL "Darwin")
    set(LZ_CACHE_LINE_SIZE 128)
    set(LZ_PAGE_SIZE 16384)       
    set(LZ_HUGE_PAGE_SIZE 33554432) 
    set(LZ_CHUNK_SHIFT 25)
endif()

# 3. Detección de Capacidades del Kernel (rseq)
check_include_file("linux/rseq.h" HAS_RSEQ_HEADER)
if(HAS_RSEQ_HEADER OR LZ_OS STREQUAL "Linux")
    set(LZ_HAS_RSEQ 1)
endif()

# 4. Inyección del Contrato Matemático
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/lz_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/lz_config.h
    @ONLY
)

# 5. Integración del Submódulo Externo (Capa 0.5)
# Esto compilará lz-logger asumiendo que tiene su propio CMakeLists.txt
add_subdirectory(external/lz-logger)

# 6. Definición del Árbol de Código (Capas 0 a 4)
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

# 7. Compilación de la Librería Compartida
add_library(lzmalloc SHARED ${LZ_LAYER0} ${LZ_LAYER1} ${LZ_LAYER2} ${LZ_LAYER3} ${LZ_LAYER4})

# 8. Simpatía Mecánica del Compilador
target_compile_options(lzmalloc PRIVATE
    -Wall -Wextra -Werror -std=c11
    -fvisibility=hidden
    -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free
    $<$<CONFIG:Release>:-O3 -march=native -mtune=native -fno-omit-frame-pointer>
    $<$<CONFIG:Debug>:-O0 -g3>
)

target_link_options(lzmalloc PRIVATE -Wl,-Bsymbolic -Wl,-z,now -Wl,-z,relro)

find_package(Threads REQUIRED)

# --- ENLACE DEL LOGGER ---
# Enlazamos la librería generada por el submódulo (lz_logger)
target_link_libraries(lzmalloc PRIVATE Threads::Threads lz_logger)

# --- INCLUDES CORREGIDOS ---
target_include_directories(lzmalloc PRIVATE 
    ${CMAKE_CURRENT_BINARY_DIR} 
    src/arch src/os src/meta src/engines src/api include
    external/lz-logger/include
)

# 9. Reglas de Instalación
install(TARGETS lzmalloc
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/lzmalloc.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/lzmalloc
)