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

/* laneCU proof harness: SFPSHFT dynamic-immediate form vs register form,
 * stratified-exhaustive per in-range shift amount k in [0,31] over all
 * 2^32 lane values.
 *
 * Context (unaryshift row adjudication): at pin-13 BOTH the fresh body
 * (fresh_cpp/unaryshift.h) and the production hand kernel
 * (ckernel_sfpu_unary_shift.h calculate_left_shift) compile the in-range
 * leg to the same 3-word row [SFPLOAD mod0=4; SFPSHFT imm-form
 * RISC-patched; SFPSTORE mod0=4] -- there is NO conversion-slot cut on
 * this row.  The residual delivery cost is the per-row RISC `sw`
 * patch of the dynamic immediate, which blocks replay capture in both
 * arms.  The delivery fix (a formation/scheduling mechanism, NOT a P2
 * dataflow cut) is: materialize the loop-invariant amount once
 * (preheader SFPLOADI, itself RISC-patched once) and switch the row to
 * the register-form SFPSHFT.  This sweep discharges that mechanism's
 * denotational obligation in advance.
 *
 * IMM form (craq-sim @ 9f324140 src/tensix.cpp:8931-8968, mod1 bit0=1):
 *   imm = signed_bits<11,0>(k); imm >= 0 -> src <<= imm & 31
 * REG form (mod1 bit0=0): s = int32(l_regs[c]) = k; s >= 0 -> src <<= s & 31
 * The out-of-range leg is a host-scalar branch in both arms (store of
 * LCONST_0); no lane obligation.
 */
#include <stdint.h>
#include <stdio.h>

static inline uint32_t shft_imm(uint32_t src, uint32_t k) {
    int32_t imm = (int32_t)(k << 20) >> 20;   /* signed_bits<11,0> */
    if (imm >= 0) return src << (imm & 31);
    return src >> ((-imm) & 31);
}
static inline uint32_t shft_reg(uint32_t src, uint32_t k) {
    int32_t s = (int32_t)k;
    if (s >= 0) return src << (s & 31);
    return src >> ((-s) & 31);
}

int main(void) {
    uint64_t total = 0;
    for (uint32_t k = 0; k <= 31; k++) {
        uint64_t mm = 0;
        for (uint64_t uw = 0; uw < 0x100000000ull; uw++) {
            uint32_t w = (uint32_t)uw;
            if (shft_imm(w, k) != shft_reg(w, k)) mm++;
        }
        printf("k=%2u mismatches=%llu\n", k, (unsigned long long)mm);
        total += mm;
    }
    printf("total mismatches      : %llu\n", (unsigned long long)total);
    printf("verdict               : %s\n", total ? "NOT-EQUAL" : "EQUAL");
    return 0;
}
