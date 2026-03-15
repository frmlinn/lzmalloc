/**
 * @file memory.h
 * @brief Interfaz de Memoria Virtual del OS.
 */
#ifndef LZ_OS_MEMORY_H
#define LZ_OS_MEMORY_H

#include <stddef.h>
#include "compiler.h"

/* -------------------------------------------------------------------------- *
 * API de Memoria Virtual (VMA)
 * -------------------------------------------------------------------------- */

/**
 * @brief Reserva memoria virtual hiper-alineada directamente del kernel.
 * @param size Tamaño total requerido (debe ser múltiplo del alignment).
 * @param alignment Frontera de alineación matemática (típicamente 2MB). Debe ser potencia de 2.
 * @return Puntero garantizado al alineamiento, o NULL si hay OS OOM.
 */
void* lz_os_alloc_aligned(size_t size, size_t alignment);

/**
 * @brief Libera el mapeo virtual y devuelve la memoria física al OS.
 * @param ptr Puntero base del mapeo.
 * @param size Tamaño total a desmapear.
 */
void lz_os_free(void* ptr, size_t size);

/**
 * @brief RSS Deflation: Devuelve memoria física sin perder el bloque virtual.
 * @param ptr Puntero a la región a purgar.
 * @param size Cantidad de bytes a purgar.
 */
void lz_os_purge_physical(void* ptr, size_t size);

#endif /* LZ_OS_MEMORY_H */