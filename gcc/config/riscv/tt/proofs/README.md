<!-- Copyright (C) 2026 Tenstorrent Inc.

     This file is part of GCC.

     GCC is free software; you can redistribute it and/or modify it
     under the terms of the GNU General Public License as published by
     the Free Software Foundation; either version 3, or (at your
     option) any later version.

     GCC is distributed in the hope that it will be useful, but WITHOUT
     ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
     or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
     License for more details.

     You should have received a copy of the GNU General Public License
     along with GCC; see the file COPYING3.  If not see
     <http://www.gnu.org/licenses/>.  -->

# tt/proofs — exhaustive denotational proofs for SFPU rewrites

Each subdirectory holds the proof obligation for one SFPU rewrite: a
host C harness that models the original instruction sequence (the
"cut") and its replacement, with instruction semantics transcribed
from the reference Tensix simulator; the harness output (RESULT.txt);
and, for some, the compiler output for the matched sequence.  Each
harness enumerates the whole input space -- every 32-bit lane
encoding, or every Dst bit pattern of a format pair -- and compares
the two sides pointwise, so an EQUAL verdict is a proof, not a
test.

A transformation that relies on one of these equivalences cites its
directory here and may fire only while that RESULT is EQUAL.  A
NOT-EQUAL result records why the corresponding rewrite is not
performed, so that it is not proposed again.

To reproduce a result, build and run the harness in its directory on
a 64-bit host and compare the output with the body of RESULT.txt:

    cc -O2 -o harness *.c -lcrypto && ./harness

(-lcrypto is needed only by the harnesses that hash their output
streams with OpenSSL.)

- cast-fp16a-rne/ — the castfp32tofp16a software round-to-nearest-even
  sequence (SFPSHFT/SFPAND/SFPIADD and a predicated move) vs
  SFP_STOCH_RND mod1=0 rnd=0.  NOT-EQUAL (33,810,429/2^32: ties-to-even
  vs ties-away, denormal flush, sign of denormals and zero, NaN
  payload), so the hardware rounding cannot replace the sequence.
- sm32-cast-elision-shift/ — leftshift SM32 software-cast chain vs the
  INT32_2S_COMP conversion-in-load (cast-free) form.  Amount dimension
  EQUAL (2^32); value dimension NOT-EQUAL (92,341,796,868 over 32x2^32,
  two exact closed-form classes, other=0; sound only at k=0 and k=31),
  so the casts cannot be elided.
- shft-imm-vs-reg/ — SFPSHFT dynamic-immediate form vs register form,
  per amount k in [0,31] x 2^32 values.  EQUAL (0 mismatches every
  stratum).  Discharges in advance the obligation of materializing a
  loop-invariant shift amount in a register; no transformation relies
  on it yet.
- cc-narrowing-writers/ — STRUCTURAL certificate (an audit, not a
  value enumeration; the obligation is decided by the simulator's
  enable-masked for_each_lane loop guard, not by operand values): the
  raw typed CC writers admitted into the audited-narrowing set by
  -mtt-tensix-optimize-cc-region-general (gimple-rvtt-invariant.cc
  cc_narrowing_modifier_p: SFPGT/SFPLE SET_CC, SFPEXEXP, SFPLZ,
  SFPIADD families) never touch a disabled lane's enable bit; SFPENCC
  and the empty-stack COMPC are recorded NOT-narrowing.  Also the
  soundness record for the tree's loop-scoped in-frame
  vocabulary-external case
  (rvtt_cc_region_tree::loop_cc_ambient_preserving_p).
- ccmask-direction-complete/ — the four float order directions vs +0.0
  of the ccmask zeroing fold: SETCC/COMPC CC lowering vs the
  SFPGT/SFPLE mod1=8 SET_DEST keep-masks (LE/GT direct-operand, LT/GE
  swapped-operand writable-zero forms).  EQUAL (0 mismatches per
  direction over 2^32; cut/hw stream commitments identical per
  direction).  LICENSES the order-direction arms of the rvtt_ccmask
  fold (gimple-rvtt-ccmask.cc).  BH-only (SFPGT/SFPLE do not exist
  before Blackhole; the pass gate is BH).
- ccmask-eqne-zero/ — the EQ/NE float directions vs +0.0 of the ccmask
  zeroing fold: the single-SETCC raw-bit lowerings (mod6 LREG_EQ0 /
  mod2 LREG_NE0) vs the two-compare SET_DEST compositions
  EQ keep = SFPOR (SFPGT (x,0), SFPGT (0,x)),
  NE keep = SFPAND (SFPLE (x,0), SFPLE (0,x)).  EQUAL (0 mismatches
  per direction over 2^32; cut/hw stream commitments identical).
  LICENSES the EQ/NE arms of the rvtt_ccmask fold under
  -mtt-tensix-optimize-ccmask AND -mtt-tensix-optimize-cc-region-general
  (gimple-rvtt-ccmask.cc).  BH-only.  The proof settles what the arms
  compute only: they also pass a delivery-cost gate
  (eqne_fold_priced_profitable_p), which refuses them
  (ccmask-eqne-fold-unprofitable) while the composition delivers more
  words than the CC skeleton it replaces.
- native-compare-gtle/ — the GT and LE float compare-against-+0.0
  arms: the SETCC web vs a single BH-native SFPGT/SFPLE SET_CC
  (mod1=1).  EQUAL (0 mismatches per direction over 2^32).  No pass
  relies on it: nothing emits that SET_CC form, and
  -mtt-tensix-optimize-native-compare has no effect.  The live compare
  lowering (gimple-rvtt-pred.cc, expand_cmp_using_gtle) takes
  arbitrary operands, commutes LT/GE into GT/LE and covers SMAG and
  integer types, which this proof does not.
- int-abs-negate-select/ — conditional-negate CC region (v_if (v<0)
  r=0-v) vs SFPABS mod1=0 integer.  EQUAL (0/2^32, INT32_MIN included;
  streams hash-identical).  LICENSES the rvtt_int_abs fold
  (-mtt-tensix-optimize-int-abs, gimple-rvtt-int-abs.cc).  Proven on
  the BH model; the simulator changes integer abs of INT32_MIN on
  later architectures, so the pass gate is BH-only.  REDUCTION.md
  records the complete set of source spellings that reduce pointwise
  to this RESULT's value function (LE polarity; GE/GT else-forms);
  they stand or fall with the RESULT.
- int-not-allones-subtract/ — one's complement stated as (-1) - v
  (SFPIADD mod1=2SCOMP|CC_NONE with all-ones minuend) vs SFPNOT.
  EQUAL (0/2^32; streams hash-identical).  LICENSES the rvtt_int_not
  fold (-mtt-tensix-optimize-int-not, gimple-rvtt-int-not.cc).  Proven
  against the TT_VERSION<=1 simulator arm that BH and WH share; the
  pass gate is BH+WH, other targets fail closed.
- stochrnd-store-round/ — SFPSTOCHRND(NEAREST, fp32->fp16b/fp16a) then
  SFPSTORE(BF16/FP16) vs the direct store, per float row.  NOT-EQUAL
  both rows (BF16 2,155,741,184/2^32; FP16 268,435,456/2^32; classes:
  finite round-up vs truncation, -0/denormal sign normalization,
  NaN->Inf): the explicit rounding instruction is semantics the
  store's own conversion path cannot reproduce, so the store-fold pass
  refuses to fold it (stochrnd-store-rounding-divergent).  Rows C and
  D cover the store the fold meets when the kernel spells an untyped
  (vFloat) Dst store, Mod0 0 = SRCB: an INDIRECTION, resolved at run
  time from the row's ALU config, so it denotes no single function.
  At the MOD0_FMT_FP32 resolution the store is EXACT and the cut is
  the IDENTITY (fp16b 4,286,513,662/2^32, fp16a 4,286,058,494/2^32 —
  every finite normal off the target lattice, plus the -0/denormal
  sign and NaN classes); the fp16a/bf16 resolutions reduce pointwise
  to rows B and A.  Cross-checks: the rows C/D direct stream
  reproduces store-sink-roundtrip's FP32/Dst32b stream, and the row D
  rounded stream reproduces cast-fp16a-rne's hw-stream.
  genrvtt-storefold refuses an indirection as a verdict key, so the
  SRCB rows are recorded as swept and refused.
- store-sink-roundtrip/ — the Dst load->store round trip per format
  pair, for the predicated store-sink arm of the store-fold pass.
  (INT32,INT32) BH raw pair EQUAL over 2^32 — LICENSES the S2 sink for
  that pair only.  BF16 (254/2^16), FP16 (2046/2^16), FP32
  (16,777,214/2^32, all denormal-flush) and the WH INT32_SM pair
  (1/2^32: -0) are NOT-EQUAL: an all-lanes write-back canonicalizes
  Dst, so eliding it is architecturally visible.  The float pairs
  refuse (store-fold-sink-format-canonicalizing) unless
  -mtt-tensix-optimize-store-sink accepts their denormal-flush
  divergence; the INT32_SM pair refuses regardless.  COVERAGE GAP: the
  generated (SRCB, SRCB) sink row derives its class as the
  most-refusing of the three DIAGONAL float pairs, which assumes the
  load and the store resolve MOD0_FMT_SRCB to the SAME concrete
  format.  SFPLOAD.md:67-82 takes SrcBFmt from
  ThreadConfig.SFPU_DEST_FMT_Base on Blackhole when
  SFPU_DEST_FMT_Enable is set; SFPSTORE.md:58-71 has no such clause
  and records its BH SrcB behaviour as "not fully characterized", so
  the two can resolve differently from one instant of config state.
  The row therefore quantifies over nine (load-resolution,
  store-resolution) cells and only three are enumerated.  `--cross'
  enumerates the other six: four are WIDTH-MISMATCH (FP32 writes a
  32-bit Dst datum against BF16/FP16's 16-bit one) and the two
  same-width cells,
  (BF16, FP16) and (FP16, BF16), are NOT-EQUAL on 65530 of 65536 Dst
  bit patterns -- far outside the denormal-flush class (254 and 2046
  on the diagonal).  The row's denormal-flush class is unsound over
  six of its nine cells.  genrvtt-storefold reads this RESULT.txt and
  stochrnd-store-round/RESULT.txt to generate the store-fold verdict
  table.
