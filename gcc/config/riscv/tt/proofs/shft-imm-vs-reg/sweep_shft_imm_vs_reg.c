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

/* SFPSHFT dynamic-immediate form vs register form, stratified-exhaustive
 * per in-range shift amount k in [0,31] over all 2^32 lane values.
 *
 * Context: both the SFPI-source unary left shift and the hand-written
 * kernel compile the in-range leg to the same 3-word row [SFPLOAD
 * mod0=4; SFPSHFT immediate form, patched with the run-time amount by
 * the RISC-V core; SFPSTORE mod0=4] (row-streams-pin13.txt) -- there is
 * no conversion to remove on this row.  Its remaining cost is the
 * per-row RISC-V store that patches the dynamic immediate, which also
 * blocks replay capture.  The fix is a scheduling change, not a
 * dataflow rewrite: materialize the loop-invariant amount once (a
 * preheader SFPLOADI, itself patched once) and switch the row to the
 * register-form SFPSHFT.  This sweep discharges that change's
 * equivalence obligation in advance.
 *
 * IMM form (mod1 bit0=1):
 *   imm = signed_bits<11,0>(k); imm >= 0 -> src <<= imm & 31
 * REG form (mod1 bit0=0): s = int32(l_regs[c]) = k; s >= 0 -> src <<= s & 31
 * The out-of-range leg is a scalar branch in both forms (a store of
 * LCONST_0); there is no per-lane obligation.
 *
 * Build: cc -O2 sweep_shft_imm_vs_reg.c -o sweep
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
