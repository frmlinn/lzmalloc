/**
 * @file security.h
 * @brief Pointer obfuscation primitives and global secrets (Safe Linking).
 */

#ifndef LZ_SECURITY_H
#define LZ_SECURITY_H

#include "common.h"

/* ========================================================================= *
 * Global State
 * ========================================================================= */

/** @brief Unique global secret generated at process bootstrap. */
extern uintptr_t g_lz_global_secret;

/* ========================================================================= *
 * Security API
 * ========================================================================= */

/**
 * @brief Initializes the security seed using cryptographic PRNG.
 * Must be called exactly once during process startup.
 */
void lz_security_init(void);

/**
 * @brief Encrypts or decrypts a pointer using XOR (Safe Linking).
 * Prevents heap-exploitation techniques like Use-After-Free list hijacking.
 * @param ptr The pointer to obfuscate/deobfuscate.
 * @param storage_addr The memory address where the pointer will be stored.
 * @return The obfuscated/deobfuscated pointer.
 */
static LZ_ALWAYS_INLINE void* lz_ptr_obfuscate(void* ptr, void* storage_addr) {
    uintptr_t p = (uintptr_t)ptr;
    uintptr_t s = (uintptr_t)storage_addr;
    return (void*)(p ^ g_lz_global_secret ^ s);
}

#endif // LZ_SECURITY_H