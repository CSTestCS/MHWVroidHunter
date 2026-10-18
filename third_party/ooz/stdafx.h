// Portability shim replacing the original MSVC-only stdafx.h of ooz so the
// decompressor builds with GCC/Clang (Linux) and MinGW-w64 (Windows).
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <emmintrin.h>
#include <xmmintrin.h>
#include <tmmintrin.h>

typedef unsigned char byte;
typedef unsigned char uint8;
typedef unsigned int uint32;
typedef unsigned long long uint64;
typedef signed long long int64;
typedef signed int int32;
typedef unsigned short uint16;
typedef signed short int16;
typedef unsigned int uint;

#ifndef _MSC_VER
#ifndef __forceinline
#define __forceinline inline __attribute__((always_inline))
#endif
static inline unsigned char ooz_bsr(unsigned long *index, uint32 mask) {
  if (!mask) return 0;
  *index = 31 - __builtin_clz(mask);
  return 1;
}
static inline unsigned char ooz_bsf(unsigned long *index, uint32 mask) {
  if (!mask) return 0;
  *index = __builtin_ctz(mask);
  return 1;
}
#define _BitScanReverse(i, m) ({ unsigned long ooz_t_ = 0; unsigned char ooz_r_ = ooz_bsr(&ooz_t_, (uint32)(m)); *(i) = ooz_t_; ooz_r_; })
#define _BitScanForward(i, m) ({ unsigned long ooz_t_ = 0; unsigned char ooz_r_ = ooz_bsf(&ooz_t_, (uint32)(m)); *(i) = ooz_t_; ooz_r_; })
static inline uint32 ooz_rotl(uint32 v, int n) { n &= 31; return n ? (v << n) | (v >> (32 - n)) : v; }
#undef _rotl
#define _rotl(v, n) ooz_rotl((uint32)(v), (int)(n))
#define _byteswap_ushort(x) __builtin_bswap16((uint16)(x))
#define _byteswap_ulong(x) __builtin_bswap32((uint32)(x))
#define _byteswap_uint64(x) __builtin_bswap64((uint64)(x))
#endif
