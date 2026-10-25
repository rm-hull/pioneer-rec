/*****************************************************
 * Portable type definitions and byte-swap utilities  *
 * for pioneer_rec (POSIX build)                     *
 *****************************************************/

#ifndef PIONEER_PORTABLE_H
#define PIONEER_PORTABLE_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Fixed-width replacements for Windows types */
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef uint8_t  BYTE;

/*
 * Byte-swap macros.
 * The on-disk format is big-endian. On little-endian hosts these swap bytes;
 * on big-endian hosts they are a no-op. Using ntohl/ntohs from <arpa/inet.h>
 * is the standard way, but that header is not available on all platforms
 * (e.g., older macOS). We use compiler builtins which are universally
 * available on gcc/clang.
 */
#if defined(__GNUC__) || defined(__clang__)
	#define B2N_32(x) __builtin_bswap32((uint32_t)(x))
	#define B2N_16(x) __builtin_bswap16((uint16_t)(x))
#else
	/* Fallback: manual bit-shift byte-swap */
	#define B2N_32(x) (((uint32_t)(x) << 24) | \
	                   (((uint32_t)(x) << 8) & 0x00FF0000U) | \
	                   (((uint32_t)(x) >> 8) & 0x0000FF00U) | \
	                   ((uint32_t)(x) >> 24))
	#define B2N_16(x) (((uint16_t)(x) << 8) | ((uint16_t)(x) >> 8))
#endif

/*
 * Safe unaligned read helpers.
 * Use memcpy to avoid undefined behaviour from unaligned pointer casts
 * on packed structs.
 */
static inline uint32_t read_u32(const void *p) {
	uint32_t v;
	memcpy(&v, p, sizeof(v));
	return v;
}

static inline uint16_t read_u16(const void *p) {
	uint16_t v;
	memcpy(&v, p, sizeof(v));
	return v;
}

#endif /* PIONEER_PORTABLE_H */
