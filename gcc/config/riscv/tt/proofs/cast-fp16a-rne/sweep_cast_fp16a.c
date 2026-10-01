/* Copyright (C) 2026 Tenstorrent Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

/* laneCT proof harness: exhaustive 2^32 denotational comparison for the
 * castfp32tofp16a proof-carrying peephole (proposal P2).
 *
 * CUT denotation: the fresh-body software-RNE dataflow cut, transcribed from
 * the pin-13 gimple of
 *   tt-metal tt_metal/tt-llk/tests/helpers/include/fresh_cpp/castfp32tofp16a.h
 * (dump cast_tu.cpp.275t.optimized, builtins sfpshft_i/sfpand/sfpiadd_v/
 *  sfploadi/sfpxicmps region):
 *   kept_lsb = (u >> 13) & 1
 *   rounded  = (u + 0x0FFF + kept_lsb) & 0xFFFFE000
 *   result   = (u & 0x7F800000) == 0x7F800000 ? u : rounded
 * Each node's semantics are the craq-sim semantics of the corresponding
 * typed insn (SFPSHFT logical-right for negative imm, SFPIADD wrap-mod-2^32,
 * SFPAND bitwise, SFPSETCC/xicmps-eq predicated move).
 *
 * HW denotation: SFP_STOCH_RND instr_mod1=0 (FP32_TO_FP16A), rnd_mode=0,
 * lifted VERBATIM from the pinned oracle craq-sim @ 9f324140,
 * src/tensix.cpp:9524-9541 (mode==0 arm) and :2772-2816 (srnd helpers),
 * with sample = STOCH_MIDPOINT (the rnd_mode==0 substitution at :9518).
 * The SFPLOADMACRO template copy at tensix.cpp:11513-11533 is a literal
 * duplicate of the same arm (checked).
 *
 * Output: per-class mismatch census + SHA256 over the full 2^32 output
 * streams of both denotations (input-order, little-endian u32), and SHA256
 * over the mismatch records (u, cut, hw).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <openssl/evp.h>

/* ---- craq-sim lift (verbatim semantics) ---- */
static const uint32_t STOCH_WIDTH = 8;
static const uint32_t STOCH_MIDPOINT = 1u << (8 - 1);

static inline uint32_t stoch_mask(uint32_t bits) {
    if (bits >= STOCH_WIDTH) return 0xFFu;
    return (0xFFu << (STOCH_WIDTH - bits)) & 0xFFu;
}
static inline uint32_t stoch_discard_count(uint32_t discarded, uint32_t bits) {
    if (bits >= STOCH_WIDTH) return (discarded >> (bits - STOCH_WIDTH)) & 0xFFu;
    return (discarded & ((1u << bits) - 1u)) << (STOCH_WIDTH - bits);
}
static inline int srnd_round_up_sample(uint32_t sample, uint32_t discarded, uint32_t bits) {
    uint32_t mask = stoch_mask(bits);
    uint32_t trunc = stoch_discard_count(discarded, bits);
    return (trunc & mask) >= (sample & mask);
}
/* tensix.cpp:9524-9541, mode==0 (FP32_TO_FP16A), sample=STOCH_MIDPOINT */
static inline uint32_t hw_stochrnd_fp16a_rne(uint32_t src) {
    uint32_t exp = (src >> 23) & 255;
    if (!exp) {
        src = 0; /* denormal/zero flushed to +0 */
    } else if (exp == 255) {
        src &= 0xFF800000; /* inf and NaN -> signed infinity */
    } else {
        uint32_t discarded_mask = 0x1FFF;
        uint32_t round_increment = 0x2000;
        uint32_t discarded_bits = src & discarded_mask;
        src -= discarded_bits;
        if (srnd_round_up_sample(STOCH_MIDPOINT, discarded_bits, 13))
            src += round_increment;
    }
    return src;
}

/* ---- fresh-body cut denotation ---- */
static inline uint32_t cut_software_rne(uint32_t u) {
    uint32_t kept_lsb = (u >> 13) & 1;            /* sfpshft_i -13; sfpand 1 */
    uint32_t rounded = (u + 0x0FFFu + kept_lsb) & 0xFFFFE000u; /* iadd_v x2; and */
    uint32_t expbits = u & 0x7F800000u;           /* sfpand upper 0x7F80 */
    return (expbits == 0x7F800000u) ? u : rounded; /* xicmps-eq predicated mov */
}

typedef struct {
    uint64_t total_mismatch;
    uint64_t tie_even;      /* normals, disc==0x1000, kept LSB even: cut down, hw up */
    uint64_t denorm_value;  /* exp==0, cut!=0 && cut!=hw, positive */
    uint64_t denorm_sign;   /* exp==0, sign bit lost (cut has sign, hw +0) */
    uint64_t nan_payload;   /* exp==255, mantissa!=0 */
    uint64_t other;
    unsigned char cut_hash[32], hw_hash[32], mm_hash[32];
    uint64_t first_examples[6][3]; int n_examples;
} result_t;

int main(void) {
    result_t R; memset(&R, 0, sizeof R);
    EVP_MD_CTX *hc = EVP_MD_CTX_new(), *hh = EVP_MD_CTX_new(), *hm = EVP_MD_CTX_new();
    EVP_DigestInit_ex(hc, EVP_sha256(), NULL);
    EVP_DigestInit_ex(hh, EVP_sha256(), NULL);
    EVP_DigestInit_ex(hm, EVP_sha256(), NULL);
    enum { CH = 1 << 20 };
    static uint32_t bc[CH], bh[CH];
    uint64_t u = 0;
    do {
        for (uint32_t i = 0; i < CH; i++, u++) {
            uint32_t x = (uint32_t)u;
            uint32_t c = cut_software_rne(x);
            uint32_t h = hw_stochrnd_fp16a_rne(x);
            bc[i] = c; bh[i] = h;
            if (c != h) {
                R.total_mismatch++;
                uint32_t exp = (x >> 23) & 255;
                uint32_t disc = x & 0x1FFF;
                if (exp == 0) {
                    if ((c & 0x7FFFFFFF) == (h & 0x7FFFFFFF)) R.denorm_sign++;
                    else R.denorm_value++;
                } else if (exp == 255) {
                    R.nan_payload++;
                } else if (disc == 0x1000 && !((x >> 13) & 1)) {
                    R.tie_even++;
                } else R.other++;
                uint32_t rec[3] = { x, c, h };
                EVP_DigestUpdate(hm, rec, sizeof rec);
                if (R.n_examples < 6 && (R.total_mismatch == R.tie_even + R.denorm_value + R.denorm_sign + R.nan_payload + R.other)) {
                    /* store one example per class on first occurrence */
                    static int seen[5];
                    int cls = exp == 0 ? ((c & 0x7FFFFFFF) == (h & 0x7FFFFFFF) ? 1 : 0) + 1
                              : exp == 255 ? 3
                              : (disc == 0x1000 && !((x >> 13) & 1)) ? 0 : 4;
                    if (!seen[cls]) {
                        seen[cls] = 1;
                        R.first_examples[R.n_examples][0] = x;
                        R.first_examples[R.n_examples][1] = c;
                        R.first_examples[R.n_examples][2] = h;
                        R.n_examples++;
                    }
                }
            }
        }
        EVP_DigestUpdate(hc, bc, sizeof bc);
        EVP_DigestUpdate(hh, bh, sizeof bh);
    } while (u != 0x100000000ull);
    unsigned int L;
    EVP_DigestFinal_ex(hc, R.cut_hash, &L);
    EVP_DigestFinal_ex(hh, R.hw_hash, &L);
    EVP_DigestFinal_ex(hm, R.mm_hash, &L);
    printf("inputs swept          : 4294967296\n");
    printf("total mismatches      : %llu\n", (unsigned long long)R.total_mismatch);
    printf("  tie-even (RNE vs half-away) : %llu\n", (unsigned long long)R.tie_even);
    printf("  denormal value (hw flush)   : %llu\n", (unsigned long long)R.denorm_value);
    printf("  denormal/zero sign lost     : %llu\n", (unsigned long long)R.denorm_sign);
    printf("  NaN payload (hw -> inf)     : %llu\n", (unsigned long long)R.nan_payload);
    printf("  other                       : %llu\n", (unsigned long long)R.other);
    printf("verdict               : %s\n", R.total_mismatch ? "NOT-EQUAL (proof obligation FAILS)" : "EQUAL");
    char hex[65];
    for (int k = 0; k < 3; k++) {
        const unsigned char *hsh = k==0 ? R.cut_hash : k==1 ? R.hw_hash : R.mm_hash;
        for (int i = 0; i < 32; i++) sprintf(hex + 2*i, "%02x", hsh[i]);
        printf("%s sha256 = %s\n", k==0?"cut-stream":k==1?"hw-stream ":"mismatches", hex);
    }
    for (int i = 0; i < R.n_examples; i++)
        printf("example: u=0x%08llx cut=0x%08llx hw=0x%08llx\n",
               (unsigned long long)R.first_examples[i][0],
               (unsigned long long)R.first_examples[i][1],
               (unsigned long long)R.first_examples[i][2]);
    return 0;
}
