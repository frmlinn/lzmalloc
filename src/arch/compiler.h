/**
 * @file compiler.h
 * @brief Directivas estrictas de optimización y layout de memoria.
 */
#ifndef LZ_COMPILER_H
#define LZ_COMPILER_H

#include "lz_config.h"

/* -------------------------------------------------------------------------- *
 * Predicción de Ramas Especulativa (Branch Target Buffer)
 * -------------------------------------------------------------------------- */
#define LZ_LIKELY(x)   __builtin_expect(!!(x), 1)
#define LZ_UNLIKELY(x) __builtin_expect(!!(x), 0)

/* -------------------------------------------------------------------------- *
 * Control del Inliner (I-Cache Optimization)
 * -------------------------------------------------------------------------- */
/** @brief Fuerza la inserción del código para no romper el pipeline con llamadas. */
#define LZ_ALWAYS_INLINE inline __attribute__((always_inline))

/** @brief Mueve la función a una sección fría del binario (.text.unlikely). */
#define LZ_COLD_PATH __attribute__((cold, noinline))

/* -------------------------------------------------------------------------- *
 * Topología y Padding Estructural
 * -------------------------------------------------------------------------- */
/** @brief Evita False Sharing separando variables en distintas líneas de caché L1/L2. */
#define LZ_CACHELINE_ALIGNED __attribute__((aligned(LZ_CACHE_LINE_SIZE)))

/** @brief Elimina el padding automático del compilador (usado en metadata densa). */
#define LZ_PACKED __attribute__((packed))

/* -------------------------------------------------------------------------- *
 * Aliasing Estricto
 * -------------------------------------------------------------------------- */
/** @brief Promesa matemática al compilador: Este puntero no se solapa con ningún otro. */
#define LZ_RESTRICT __restrict__

#endif /* LZ_COMPILER_H */