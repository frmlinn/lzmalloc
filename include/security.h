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

/** @brief Unique global secret generated cryptographically at process bootstrap. */
extern uintptr_t g_lz_global_secret;

/* ========================================================================= *
 * Security API
 * ========================================================================= */

/**
 * @brief Bootstraps the security seed using a cryptographic PRNG.
 * @note Must be invoked exactly once during the process startup phase.
 */
void lz_security_init(void);

/**
 * @brief Encrypts or decrypts a pointer using fast XOR operations (Safe Linking).
 * Prevents advanced heap-exploitation techniques such as Use-After-Free list hijacking.
 * * @param ptr The target pointer to obfuscate/deobfuscate.
 * @param storage_addr The memory address where the pointer will physically reside.
 * @return The cryptographically masked pointer.
 */
static LZ_ALWAYS_INLINE void* lz_ptr_obfuscate(void* ptr, void* storage_addr) {
    uintptr_t p = (uintptr_t)ptr;
    uintptr_t s = (uintptr_t)storage_addr;
    return (void*)(p ^ g_lz_global_secret ^ s);
}

#endif /* LZ_SECURITY_H */