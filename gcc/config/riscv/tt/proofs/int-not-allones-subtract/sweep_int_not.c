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

/* laneEK proof harness: exhaustive 2^32 denotational comparison for the
 * SFPNOT instruction-selection admission (the laneCZ simple-unary
 * derive-core admission class; template = laneCU sweep_int_abs.c).
 *
 * CUT denotation: the fresh-body one's-complement statement, transcribed
 * from the pin-15 gimple of
 *   tt-metal tt_metal/tt-llk/tests/helpers/include/fresh_cpp/bitwisenot.h
 * (dump at the rvtt_int_abs pipeline position; emitted row stream
 *  SFPLOAD mod0=4 / SFPIADD mod1=6 vs hoisted SFPLOADI -1 / SFPSTORE
 *  mod0=4):
 *   allones = 0xFFFFFFFF               (sfpxloadi value 4294967295)
 *   cut     = allones - w  (wrap)      (sfpiadd_v (w, allones, 6):
 *                                       mod1 = ARG_2SCOMP_LREG_DST(2)
 *                                            | CC_NONE(4);
 *                                       craq-sim tensix.cpp:8894-8929,
 *                                       the mod1&2 arm: src(=lreg_c=allones)
 *                                       -= LReg[lreg_dest](=w); no CC
 *                                       write since mod1&4)
 *
 * HW denotation: SFPNOT, lifted VERBATIM from the pinned oracles
 * craq-sim @ 9f324140 (BH libttsim 32489dda..., WH 8f0079a9... -- the
 * function is shared, carries NO TT_VERSION guard), src/tensix.cpp:9102:
 *   TENSIX_EXECUTE_SFPNOT() -> tensix_execute_sfpu_int32 (:9051-9062)
 *   with op = [](src_b, src_c) { return ~src_c; }
 * i.e. dest = ~LReg[lreg_c] lanewise under the live CC mask.
 *
 * Predication equivalence (not swept -- structural, cited for the pass):
 * both arms write lreg_dest only on CC-enabled lanes via the identical
 * for_each_lane(mask) walk (tensix.cpp:8901 vs :9055), both verify
 * lreg_dest < 8, and neither writes CC (the matched SFPIADD mod carries
 * CC_NONE; SFPNOT has no CC logic at all).
 *
 * Output: per-class mismatch census (expected all-zero => EQUAL) + SHA256
 * over the full 2^32 output streams of both denotations (input-order,
 * little-endian u32).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <openssl/evp.h>

/* ---- fresh-body cut denotation (gimple lowering) ---- */
static inline uint32_t cut_allones_subtract(uint32_t w) {
    uint32_t allones = 0xFFFFFFFFu;   /* sfpxloadi 4294967295 */
    return allones - w;               /* sfpiadd_v mod1=6: C - D, wrap */
}

/* ---- craq-sim lift (verbatim semantics, tensix.cpp:9102-9104) ---- */
static inline uint32_t hw_sfpnot(uint32_t src_c) {
    return ~src_c;
}

int main(void) {
    uint64_t total_mismatch = 0;
    uint64_t mm_zero = 0;      /* w == 0            */
    uint64_t mm_allones = 0;   /* w == 0xFFFFFFFF   */
    uint64_t mm_neg = 0;       /* bit31 set, other  */
    uint64_t mm_pos = 0;       /* bit31 clear, other*/
    EVP_MD_CTX *hc = EVP_MD_CTX_new(), *hh = EVP_MD_CTX_new();
    EVP_DigestInit_ex(hc, EVP_sha256(), NULL);
    EVP_DigestInit_ex(hh, EVP_sha256(), NULL);
    enum { CH = 1 << 20 };
    static uint32_t bc[CH], bh[CH];
    uint64_t u = 0;
    do {
        for (uint32_t i = 0; i < CH; i++, u++) {
            uint32_t x = (uint32_t)u;
            uint32_t c = cut_allones_subtract(x);
            uint32_t h = hw_sfpnot(x);
            bc[i] = c; bh[i] = h;
            if (c != h) {
                total_mismatch++;
                if (x == 0) mm_zero++;
                else if (x == 0xFFFFFFFFu) mm_allones++;
                else if (x & 0x80000000u) mm_neg++;
                else mm_pos++;
            }
        }
        EVP_DigestUpdate(hc, bc, sizeof bc);
        EVP_DigestUpdate(hh, bh, sizeof bh);
    } while (u != 0x100000000ull);
    unsigned char dc[32], dh[32]; unsigned int L;
    EVP_DigestFinal_ex(hc, dc, &L);
    EVP_DigestFinal_ex(hh, dh, &L);
    printf("inputs swept          : %llu\n", (unsigned long long)u);
    printf("total mismatches      : %llu\n", (unsigned long long)total_mismatch);
    printf("  zero                        : %llu\n", (unsigned long long)mm_zero);
    printf("  all-ones (0xFFFFFFFF)       : %llu\n", (unsigned long long)mm_allones);
    printf("  negative (bit31 set)        : %llu\n", (unsigned long long)mm_neg);
    printf("  positive (bit31 clear)      : %llu\n", (unsigned long long)mm_pos);
    printf("verdict               : %s\n", total_mismatch ? "NOT-EQUAL" : "EQUAL");
    printf("cut-stream sha256 = ");
    for (int i = 0; i < 32; i++) printf("%02x", dc[i]);
    printf("\nhw-stream  sha256 = ");
    for (int i = 0; i < 32; i++) printf("%02x", dh[i]);
    printf("\n");
    EVP_MD_CTX_free(hc);
    EVP_MD_CTX_free(hh);
    return total_mismatch ? 1 : 0;
}
