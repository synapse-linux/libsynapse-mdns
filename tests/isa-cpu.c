// SPDX-License-Identifier: MIT
/* Test-only execution capability probe; never installed or run by normal tests. */
#include <cpuid.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (!__get_cpuid(1, &a, &b, &c, &d)) return 1;
    const uint32_t base = (1U << 0) | bit_CMPXCHG8B | bit_CMOV | bit_MMX | bit_SSE | bit_SSE2;
    const uint32_t extra = bit_SSE3 | bit_PCLMUL | bit_SSSE3 | bit_FMA | bit_CMPXCHG16B |
                           bit_SSE4_1 | bit_SSE4_2 | bit_MOVBE | bit_POPCNT | bit_AES |
                           bit_XSAVE | bit_AVX | bit_F16C | bit_RDRND;
    if ((d & base) != base || (c & extra) != 0) return 1;
    if (__get_cpuid_count(7, 0, &a, &b, &c, &d)) {
        const uint32_t extended = bit_BMI | bit_AVX2 | bit_BMI2 | bit_AVX512F |
                                  bit_AVX512DQ | bit_AVX512CD | bit_AVX512BW | bit_AVX512VL;
        if ((b & extended) != 0) return 1;
    }
    if (__get_cpuid(0x80000001U, &a, &b, &c, &d)) {
        /* LAHF/SAHF, ABM, SSE4a, XOP, FMA4 and TBM are all beyond baseline. */
        const uint32_t extended = (1U << 0) | (1U << 5) | (1U << 6) | (1U << 11) |
                                  (1U << 16) | (1U << 21);
        if ((c & extended) != 0) return 1;
    }
    puts("synapse.isa.cpu/v1: x86-64-baseline");
    return 0;
}
