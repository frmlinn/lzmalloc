/**
 * @file security.h
 * @brief Criptografía de Punteros (Safe Linking) e inicialización de entropía.
 */
#ifndef LZ_OS_SECURITY_H
#define LZ_OS_SECURITY_H

#include <stdint.h>
#include "compiler.h"

/* -------------------------------------------------------------------------- *
 * Estado Global de Entropía
 * -------------------------------------------------------------------------- */
extern uintptr_t g_lz_global_secret;

/* -------------------------------------------------------------------------- *
 * API de Seguridad
 * -------------------------------------------------------------------------- */

/**
 * @brief Bootstraps el CSPRNG del OS para obtener la semilla global.
 * @note Thread-safe. Garantiza inicialización única.
 */
void lz_security_init(void);

/**
 * @brief Ofusca/Desofusca un puntero utilizando Safe Linking y Entropía Global.
 * @param ptr El puntero objetivo (ej. el puntero `next` de un free-list).
 * @param storage_addr La dirección física donde reside el puntero.
 * @return Puntero ofuscado (XOR matemático).
 */
static LZ_ALWAYS_INLINE void* lz_ptr_obfuscate(void* ptr, void* volatile* storage_addr) {
    uintptr_t p = (uintptr_t)ptr;
    uintptr_t s = (uintptr_t)storage_addr;
    
    /* Mix de entropía: Puntero ^ Semilla Criptográfica ^ (ASLR shift) 
     * El shift de 4 asume alineación de 16-bytes (los últimos 4 bits siempre son 0). */
    return (void*)(p ^ g_lz_global_secret ^ (s >> 4));
}

#endif /* LZ_OS_SECURITY_H */