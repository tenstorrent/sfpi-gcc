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
