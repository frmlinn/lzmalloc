/**
 * @file chunk.h
 * @brief Layout estricto de la cabecera del Superbloque (Chunk) de 2MB.
 */
#ifndef LZ_META_CHUNK_H
#define LZ_META_CHUNK_H

#include <stdint.h>
#include "compiler.h"
#include "lz_config.h"

/* -------------------------------------------------------------------------- *
 * Identidad y Tipología
 * -------------------------------------------------------------------------- */

/* Magic Number: "LZMALLOC" en ASCII Hexadecimal (Little Endian) */
#define LZ_CHUNK_MAGIC_V2 0x434F4C4C414D5A4CULL

/* Enrutamiento de los motores de la Capa 3 */
#define LZ_CHUNK_TYPE_SLAB   0
#define LZ_CHUNK_TYPE_SPAN   1
#define LZ_CHUNK_TYPE_DIRECT 2

/* -------------------------------------------------------------------------- *
 * Geometría de Datos (Mechanical Sympathy)
 * -------------------------------------------------------------------------- */

/**
 * @struct lz_chunk_header_s
 * @brief Metadata residente en el offset 0x0 de cada bloque hiper-alineado.
 * @note Obligatoriamente rellenado (padded) para coincidir exactamente con 
 * el tamaño de la línea de caché L1/L2 configurada (64 o 128 bytes).
 */
typedef struct lz_chunk_header_s {
    /* --- HOT DATA: 16 Bytes (Offset 0x0) --- */
    /* El Magic Number DEBE ser el primer campo. lz_meta_resolve() lo lee
     * asumiendo que está en el inicio matemático del bloque. */
    uint64_t magic;
    
    /* Topología: Define a qué Core-Local Heap pertenece este bloque */
    uint32_t core_id;
    uint32_t chunk_type;

    /* --- COLD DATA: 16 Bytes (Offset 0x10) --- */
    /* Puntero intrusivo para el Treiber Stack lock-free del VMM global */
    struct lz_chunk_header_s* next;
    
    /* Canario criptográfico para detectar desbordamientos masivos (Buffer Overflows) */
    uint64_t canary;

    /* --- HARDWARE PADDING --- */
    /* Saturamos el resto de la línea de caché para evitar que el payload del 
     * usuario (u otro metadata) comparta esta línea y provoque False Sharing. */
    uint8_t _padding[LZ_CACHE_LINE_SIZE - 32];

} LZ_CACHELINE_ALIGNED lz_chunk_t;

/* -------------------------------------------------------------------------- *
 * Validación Estática (Compile-Time Assertions)
 * -------------------------------------------------------------------------- */

/* Promesa inviolable al silicio: Si algún ingeniero añade un campo y rompe
 * las matemáticas de la línea de caché, la compilación fallará inmediatamente. */
_Static_assert(sizeof(lz_chunk_t) == LZ_CACHE_LINE_SIZE, 
    "Violación topológica: lz_chunk_t no satura exactamente la línea de caché");

#endif /* LZ_META_CHUNK_H */