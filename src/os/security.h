/**
 * @file security.h
 * @brief Pointer Cryptography and Entropy Initialization.
 * @details Implements the "Safe Linking" mitigation to prevent free-list 
 * poisoning and heap-based exploitation techniques.
 */
#ifndef LZ_OS_SECURITY_H
#define LZ_OS_SECURITY_H

#include <stdint.h>
#include "compiler.h"

/* -------------------------------------------------------------------------- *
 * Global Entropy State
 * -------------------------------------------------------------------------- */
/** @brief Process-wide unique secret used as a masking seed for Safe Linking. */
extern uintptr_t g_lz_global_secret;

/* -------------------------------------------------------------------------- *
 * Security API
 * -------------------------------------------------------------------------- */

/**
 * @brief Bootstraps the OS CSPRNG to acquire the global secret.
 * @details Thread-safe and idempotent. Ensures the secret is only set once 
 * during process startup.
 */
void lz_security_init(void);

/**
 * @brief Obfuscates or de-obfuscates a pointer using Safe Linking.
 * @details Mixes the pointer value with a global secret and the storage 
 * address (ASLR-derived) to make heap metadata non-deterministic.
 * @param ptr Target pointer (e.g., the 'next' pointer in a free-list).
 * @param storage_addr Memory address where the pointer is physically stored.
 * @return The cryptographically masked pointer (via XOR).
 */
static LZ_ALWAYS_INLINE void* lz_ptr_obfuscate(void* ptr, void* volatile* storage_addr) {
    uintptr_t p = (uintptr_t)ptr;
    uintptr_t s = (uintptr_t)storage_addr;
    
    /* Entropy Mix: Pointer ^ Global Secret ^ (Storage Address >> 4) 
     * Shift of 4 bits assumes 16-byte alignment (LSB are always zero). */
    return (void*)(p ^ g_lz_global_secret ^ (s >> 4));
}

#endif /* LZ_OS_SECURITY_H */