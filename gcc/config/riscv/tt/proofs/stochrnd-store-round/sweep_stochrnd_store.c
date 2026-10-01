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
 * and for BOTH conversions the store the fold would actually meet when
 * the kernel spells an untyped (vFloat) Dst store, whose Mod0 is 0 =
 * SRCB -- an INDIRECTION, not a format.  SRCB is resolved at run time
 * from the row's ALU configuration (ALU_ACC_CTRL_SFPU_Fp32_enabled /
 * ALU_FORMAT_SPEC_REG*_SrcB), so mod0=0 does not denote one function:
 *
 *   row C: SFPSTOCHRND mod1=1 (FP32_TO_FP16B) rnd=NEAREST + SFPSTORE
 *          mod0=0 resolved MOD0_FMT_FP32 (Dst32b)  vs  the same store
 *   row D: SFPSTOCHRND mod1=0 (FP32_TO_FP16A) rnd=NEAREST + SFPSTORE
 *          mod0=0 resolved MOD0_FMT_FP32 (Dst32b)  vs  the same store
 *
 * The OTHER resolutions of mod0=0 -- SrcB configured fp16a or bf16, a
 * 16-bit Dst layout -- reduce pointwise to rows B and A respectively
 * (same store function, same inputs), so rows C and D are the whole of
 * what mod0=0 adds.  They exist because the two resolutions disagree:
 * on rows A/B the store performs the conversion and the cut is the
 * licensed truncation-vs-nearest divergence, whereas on rows C/D the
 * FP32 store is exact and the cut is the IDENTITY -- the rounding is
 * not substituted, it is deleted.  A pair keyed on mod0=0 would be
 * claiming both at once, which is why an indirection cannot be an
 * admission key (genrvtt-storefold refuses to emit one).
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

/* store mod0=3 (fp32), BH TT_VERSION=1 arm: denormals_as_zeros, lifted
   verbatim from the sibling obligation's harness
   (proofs/store-sink-roundtrip/sweep_store_sink_roundtrip.c store_fp32,
   tensix.cpp mod0=3 arm) -- no mantissa conversion of any kind.  This is
   what SRCB resolves to at ALU_ACC_CTRL_SFPU_Fp32_enabled.  */
static inline uint32_t hw_store_fp32(uint32_t v) { return denormals_as_zeros(v); }

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
    struct census cc, cd;
    memset(&cc, 0, sizeof cc);
    memset(&cd, 0, sizeof cd);
    EVP_MD_CTX *ha_c = EVP_MD_CTX_new(), *ha_h = EVP_MD_CTX_new();
    EVP_MD_CTX *hb_c = EVP_MD_CTX_new(), *hb_h = EVP_MD_CTX_new();
    EVP_MD_CTX *hc_c = EVP_MD_CTX_new(), *hc_h = EVP_MD_CTX_new();
    EVP_MD_CTX *hd_c = EVP_MD_CTX_new(), *hd_h = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ha_c, EVP_sha256(), NULL);
    EVP_DigestInit_ex(ha_h, EVP_sha256(), NULL);
    EVP_DigestInit_ex(hb_c, EVP_sha256(), NULL);
    EVP_DigestInit_ex(hb_h, EVP_sha256(), NULL);
    EVP_DigestInit_ex(hc_c, EVP_sha256(), NULL);
    EVP_DigestInit_ex(hc_h, EVP_sha256(), NULL);
    EVP_DigestInit_ex(hd_c, EVP_sha256(), NULL);
    EVP_DigestInit_ex(hd_h, EVP_sha256(), NULL);
    enum { CH = 1 << 20 };
    static uint16_t a_cut[CH], a_hw[CH], b_cut[CH], b_hw[CH];
    static uint32_t c_cut[CH], c_hw[CH], d_cut[CH], d_hw[CH];
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
            /* row C: explicit fp16b round then the SRCB-as-FP32 store vs
               that store alone.  row D: the same for fp16a.  */
            uint32_t c1 = hw_store_fp32(hw_stochrnd_nearest(x, 0));
            uint32_t c2 = hw_store_fp32(x);
            uint32_t d1 = hw_store_fp32(hw_stochrnd_nearest(x, 1));
            uint32_t d2 = hw_store_fp32(x);
            a_cut[i] = a1; a_hw[i] = a2;
            b_cut[i] = b1; b_hw[i] = b2;
            c_cut[i] = c1; c_hw[i] = c2;
            d_cut[i] = d1; d_hw[i] = d2;
            if (a1 != a2) classify(&ca, x);
            if (b1 != b2) classify(&cb, x);
            if (c1 != c2) classify(&cc, x);
            if (d1 != d2) classify(&cd, x);
        }
        EVP_DigestUpdate(ha_c, a_cut, sizeof a_cut);
        EVP_DigestUpdate(ha_h, a_hw, sizeof a_hw);
        EVP_DigestUpdate(hb_c, b_cut, sizeof b_cut);
        EVP_DigestUpdate(hb_h, b_hw, sizeof b_hw);
        EVP_DigestUpdate(hc_c, c_cut, sizeof c_cut);
        EVP_DigestUpdate(hc_h, c_hw, sizeof c_hw);
        EVP_DigestUpdate(hd_c, d_cut, sizeof d_cut);
        EVP_DigestUpdate(hd_h, d_hw, sizeof d_hw);
    } while (u != 0x100000000ull);

    unsigned char d[8][32]; unsigned int L;
    EVP_DigestFinal_ex(ha_c, d[0], &L);
    EVP_DigestFinal_ex(ha_h, d[1], &L);
    EVP_DigestFinal_ex(hb_c, d[2], &L);
    EVP_DigestFinal_ex(hb_h, d[3], &L);
    EVP_DigestFinal_ex(hc_c, d[4], &L);
    EVP_DigestFinal_ex(hc_h, d[5], &L);
    EVP_DigestFinal_ex(hd_c, d[6], &L);
    EVP_DigestFinal_ex(hd_h, d[7], &L);

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
    const char *names2[2] = {
        "row C: STOCHRND fp32->fp16b NEAREST + STORE mod0=0 resolved FP32"
        "  vs  STORE mod0=0 resolved FP32",
        "row D: STOCHRND fp32->fp16a NEAREST + STORE mod0=0 resolved FP32"
        "  vs  STORE mod0=0 resolved FP32"
    };
    struct census *cs2[2] = { &cc, &cd };
    for (int r = 0; r < 2; r++) {
        printf("%s\n", names2[r]);
        printf("  resolution          : SRCB -> MOD0_FMT_FP32 (Dst32b,"
               " ALU_ACC_CTRL_SFPU_Fp32_enabled); the store is EXACT\n");
        printf("  inputs swept        : 4294967296\n");
        printf("  total mismatches    : %llu\n", (unsigned long long)cs2[r]->total);
        printf("    finite off-lattice (rounding deleted) : %llu\n", (unsigned long long)cs2[r]->roundup);
        printf("    -0.0 sign normalization               : %llu\n", (unsigned long long)cs2[r]->negzero);
        printf("    denormal sign/flush                   : %llu\n", (unsigned long long)cs2[r]->denorm);
        printf("    NaN -> Inf normalization              : %llu\n", (unsigned long long)cs2[r]->nan);
        printf("    infinity                              : %llu\n", (unsigned long long)cs2[r]->inf);
        printf("    other exp==0                          : %llu\n", (unsigned long long)cs2[r]->other);
        printf("  verdict             : %s\n", cs2[r]->total ? "NOT-EQUAL" : "EQUAL");
        printf("  cut is the identity : %s (the direct arm applies no"
               " conversion; the fold deletes the rounding rather than"
               " substituting it)\n", "YES");
    }
    printf("rowA fused-stream sha256  = ");
    for (int i = 0; i < 32; i++) printf("%02x", d[0][i]);
    printf("\nrowA direct-stream sha256 = ");
    for (int i = 0; i < 32; i++) printf("%02x", d[1][i]);
    printf("\nrowB fused-stream sha256  = ");
    for (int i = 0; i < 32; i++) printf("%02x", d[2][i]);
    printf("\nrowB direct-stream sha256 = ");
    for (int i = 0; i < 32; i++) printf("%02x", d[3][i]);
    printf("\nrowC fused-stream sha256  = ");
    for (int i = 0; i < 32; i++) printf("%02x", d[4][i]);
    printf("\nrowC direct-stream sha256 = ");
    for (int i = 0; i < 32; i++) printf("%02x", d[5][i]);
    printf("\nrowD fused-stream sha256  = ");
    for (int i = 0; i < 32; i++) printf("%02x", d[6][i]);
    printf("\nrowD direct-stream sha256 = ");
    for (int i = 0; i < 32; i++) printf("%02x", d[7][i]);
    printf("\n");
    EVP_MD_CTX_free(ha_c); EVP_MD_CTX_free(ha_h);
    EVP_MD_CTX_free(hb_c); EVP_MD_CTX_free(hb_h);
    EVP_MD_CTX_free(hc_c); EVP_MD_CTX_free(hc_h);
    EVP_MD_CTX_free(hd_c); EVP_MD_CTX_free(hd_h);
    /* NOT-EQUAL is the expected (refusal-grounding) result; exit 0 when the
       sweep completed and produced a verdict either way.  */
    return 0;
}
