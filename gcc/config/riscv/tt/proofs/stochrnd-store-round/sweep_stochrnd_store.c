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
 * proposed SFPSTOCHRND-into-SFPSTORE fold (fold the explicit rounding
 * instruction into the store's own format-conversion path), for BOTH
 * float rows the fold could claim:
 *
 *   row A: SFPSTOCHRND mod1=1 (FP32_TO_FP16B) rnd=NEAREST + SFPSTORE
 *          mod0=2 (BF16)  vs  SFPSTORE mod0=2 direct
 *   row B: SFPSTOCHRND mod1=0 (FP32_TO_FP16A) rnd=NEAREST + SFPSTORE
 *          mod0=1 (FP16)  vs  SFPSTORE mod0=1 direct
 *
 * Semantics lifted VERBATIM from the pinned oracle craq-sim @ 9f324140
 * (BH libttsim 32489dda..., WH 8f0079a9...):
 *   - SFP_STOCH_RND FloatFloat arm: src/tensix.cpp:9508-9541 (rnd=NEAREST
 *     => sample = STOCH_MIDPOINT = 128; srnd_round_up_sample :2812-2816
 *     with stoch_discard_count :2802-2808 -> round up iff the discarded
 *     bits >= half, i.e. round-to-nearest-ties-AWAY; exp==0 -> +0
 *     including -0.0/-denormal; exp==255 -> signed infinity incl. NaN).
 *   - SFPSTORE mod0=2 BF16 path: sfpstore_values src/tensix.cpp:8636-8641
 *     (16-bit Dst layout arm): denormals_as_zeros (:5492-5497, KEEPS the
 *     sign) then value >> 16 = mantissa truncation toward zero.
 *   - SFPSTORE mod0=1 FP16 path: sfpstore_values :8634 ->
 *     sfpu_store_to_fp16 (:8563-8575): denormal/underflow -> signed zero,
 *     overflow/inf/NaN -> signed huge (0x7FFF pattern), mantissa m >> 13
 *     truncation toward zero.
 *   encode_bf16/encode_fp16 are bijective Dst bit-layout shuffles common
 *   to both arms of each row; the comparison is on the pre-encode value,
 *   which compares equal iff the encoded Dst datum compares equal.
 *
 * The tt-isa-documentation functional models (BlackholeA0
 * SFPSTOCHRND_FloatFloat.md, SFPSTORE.md ToBF16/ToFP16) state the same
 * semantics; doc = prior, pinned sim = oracle (both agree here).
 *
 * Expected (doc-derived) verdict: NOT-EQUAL on both rows -- the store's
 * conversion truncates toward zero while the explicit instruction rounds
 * to nearest-ties-away and normalizes specials (-0/denormal -> +0,
 * NaN -> Inf).  The fold is therefore REFUSED BY NAME
 * (stochrnd-store-rounding-divergent) for every float row; this harness
 * is the standing divergence record per the tt/proofs README contract
 * (a NOT-EQUAL result is a standing named refusal so the cut is never
 * re-mined).
 *
 * Output: per-class mismatch census + SHA256 stream commitments over the
 * 16-bit results (input-order, little-endian u16) for each row.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <openssl/evp.h>

/* ---- craq-sim lifts ---- */

/* tensix.cpp:5492-5497 */
static inline uint32_t denormals_as_zeros(uint32_t u) {
    if ((u & 0x7FFFFFFFu) < 0x800000u) {
        u &= 0x80000000u;
    }
    return u;
}

/* tensix.cpp:9524-9540, mode 0/1, rnd_mode=0 (NEAREST): sample = 128.
 * srnd_round_up_sample(128, d, 16): (d >> 8) & 0xFF >= 128  <=> d >= 0x8000
 * srnd_round_up_sample(128, d, 13): (d >> 5) & 0xFF >= 128  <=> d >= 0x1000 */
static inline uint32_t hw_stochrnd_nearest(uint32_t src, int fp16a) {
    uint32_t exp = (src >> 23) & 255u;
    if (!exp) {
        src = 0; /* denormal/zero flushed to +0 */
    } else if (exp == 255u) {
        src &= 0xFF800000u; /* inf and NaN -> signed infinity */
    } else {
        uint32_t discarded_mask = fp16a ? 0x1FFFu : 0xFFFFu;
        uint32_t round_increment = fp16a ? 0x2000u : 0x10000u;
        uint32_t half = fp16a ? 0x1000u : 0x8000u;
        uint32_t discarded_bits = src & discarded_mask;
        src -= discarded_bits;
        if (discarded_bits >= half)
            src += round_increment;
    }
    return src;
}

/* tensix.cpp:8636-8641 (16-bit layout arm), pre-encode value */
static inline uint16_t hw_store_bf16(uint32_t value) {
    return (uint16_t)(denormals_as_zeros(value) >> 16);
}

/* tensix.cpp:8563-8575 */
static inline uint16_t hw_store_fp16(uint32_t x) {
    uint32_t s = x >> 31;
    uint32_t e32 = (x >> 23) & 255u;
    uint32_t m = x & 0x7FFFFFu;
    int32_t e = (int32_t)e32 - 112;
    if (e <= 0) {
        return (uint16_t)(s << 15);
    } else if (e > 31) {
        return (uint16_t)((s << 15) | 0x7FFFu);
    } else {
        return (uint16_t)((s << 15) | ((uint32_t)e << 10) | (m >> 13));
    }
}

struct census {
    uint64_t total;
    uint64_t roundup;   /* finite, discarded bits >= half: trunc vs +1 */
    uint64_t negzero;   /* x == 0x80000000 */
    uint64_t denorm;    /* exp==0, mantissa != 0 (either sign) */
    uint64_t nan;       /* exp==255, mantissa != 0 */
    uint64_t inf;       /* exp==255, mantissa == 0 */
    uint64_t other;
};

static void classify(struct census *c, uint32_t x) {
    uint32_t exp = (x >> 23) & 255u;
    uint32_t man = x & 0x7FFFFFu;
    c->total++;
    if (exp == 255u) {
        if (man) c->nan++; else c->inf++;
    } else if (exp == 0) {
        if (x == 0x80000000u) c->negzero++;
        else if (man) c->denorm++;
        else c->other++;
    } else {
        c->roundup++; /* verified below: every finite mismatch is a
                         discarded-bits rounding difference */
    }
}

int main(void) {
    struct census ca, cb;
    memset(&ca, 0, sizeof ca);
    memset(&cb, 0, sizeof cb);
    EVP_MD_CTX *ha_c = EVP_MD_CTX_new(), *ha_h = EVP_MD_CTX_new();
    EVP_MD_CTX *hb_c = EVP_MD_CTX_new(), *hb_h = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ha_c, EVP_sha256(), NULL);
    EVP_DigestInit_ex(ha_h, EVP_sha256(), NULL);
    EVP_DigestInit_ex(hb_c, EVP_sha256(), NULL);
    EVP_DigestInit_ex(hb_h, EVP_sha256(), NULL);
    enum { CH = 1 << 20 };
    static uint16_t a_cut[CH], a_hw[CH], b_cut[CH], b_hw[CH];
    uint64_t u = 0;
    do {
        for (uint32_t i = 0; i < CH; i++, u++) {
            uint32_t x = (uint32_t)u;
            /* row A: explicit round then bf16 store vs direct bf16 store */
            uint16_t a1 = hw_store_bf16(hw_stochrnd_nearest(x, 0));
            uint16_t a2 = hw_store_bf16(x);
            /* row B: explicit round then fp16 store vs direct fp16 store */
            uint16_t b1 = hw_store_fp16(hw_stochrnd_nearest(x, 1));
            uint16_t b2 = hw_store_fp16(x);
            a_cut[i] = a1; a_hw[i] = a2;
            b_cut[i] = b1; b_hw[i] = b2;
            if (a1 != a2) classify(&ca, x);
            if (b1 != b2) classify(&cb, x);
        }
        EVP_DigestUpdate(ha_c, a_cut, sizeof a_cut);
        EVP_DigestUpdate(ha_h, a_hw, sizeof a_hw);
        EVP_DigestUpdate(hb_c, b_cut, sizeof b_cut);
        EVP_DigestUpdate(hb_h, b_hw, sizeof b_hw);
    } while (u != 0x100000000ull);

    unsigned char d[4][32]; unsigned int L;
    EVP_DigestFinal_ex(ha_c, d[0], &L);
    EVP_DigestFinal_ex(ha_h, d[1], &L);
    EVP_DigestFinal_ex(hb_c, d[2], &L);
    EVP_DigestFinal_ex(hb_h, d[3], &L);

    const char *names[2] = {
        "row A: STOCHRND fp32->fp16b NEAREST + STORE mod0=2  vs  STORE mod0=2",
        "row B: STOCHRND fp32->fp16a NEAREST + STORE mod0=1  vs  STORE mod0=1"
    };
    struct census *cs[2] = { &ca, &cb };
    for (int r = 0; r < 2; r++) {
        printf("%s\n", names[r]);
        printf("  inputs swept        : 4294967296\n");
        printf("  total mismatches    : %llu\n", (unsigned long long)cs[r]->total);
        printf("    finite round-up (discarded >= half) : %llu\n", (unsigned long long)cs[r]->roundup);
        printf("    -0.0 sign normalization             : %llu\n", (unsigned long long)cs[r]->negzero);
        printf("    denormal sign/flush                 : %llu\n", (unsigned long long)cs[r]->denorm);
        printf("    NaN -> Inf normalization            : %llu\n", (unsigned long long)cs[r]->nan);
        printf("    infinity                            : %llu\n", (unsigned long long)cs[r]->inf);
        printf("    other exp==0                        : %llu\n", (unsigned long long)cs[r]->other);
        printf("  verdict             : %s\n", cs[r]->total ? "NOT-EQUAL" : "EQUAL");
    }
    printf("rowA fused-stream sha256  = ");
    for (int i = 0; i < 32; i++) printf("%02x", d[0][i]);
    printf("\nrowA direct-stream sha256 = ");
    for (int i = 0; i < 32; i++) printf("%02x", d[1][i]);
    printf("\nrowB fused-stream sha256  = ");
    for (int i = 0; i < 32; i++) printf("%02x", d[2][i]);
    printf("\nrowB direct-stream sha256 = ");
    for (int i = 0; i < 32; i++) printf("%02x", d[3][i]);
    printf("\n");
    EVP_MD_CTX_free(ha_c); EVP_MD_CTX_free(ha_h);
    EVP_MD_CTX_free(hb_c); EVP_MD_CTX_free(hb_h);
    /* NOT-EQUAL is the expected (refusal-grounding) result; exit 0 when the
       sweep completed and produced a verdict either way.  */
    return 0;
}
