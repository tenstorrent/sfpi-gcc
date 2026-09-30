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

# tt/proofs — exhaustive denotational proof artifacts (proposal P2)

Each subdirectory carries the proof obligation record for one proposed
proof-carrying peephole: the harness (host C, oracle semantics lifted
verbatim from the pinned reference simulator with file:line provenance), the swept
result with SHA256 stream commitments, and the matched cut's gimple.
A rule may ship in rvtt.gc ONLY citing a directory here whose RESULT is
EQUAL over the full input space; a NOT-EQUAL result is a standing named
refusal (a permanent row in tt/rvtt-refusals.def) so the cut is never
re-mined.

- cast-fp16a-rne/ — castfp32tofp16a software-RNE cut vs SFP_STOCH_RND
  mod1=0 rnd=0. NOT-EQUAL (33,810,429/2^32). Refusal:
  cast-cut-equivalence-refuted. proved 2026-08-20.
- int-abs-negate-select/ — conditional-negate CC region (v_if (v<0)
  r=0-v) vs SFPABS mod1=0 integer. EQUAL (0/2^32, INT32_MIN included;
  streams hash-identical). LICENSES the rvtt_int_abs fold
  (-mtt-tensix-optimize-int-abs, gimple-rvtt-int-abs.cc); the fold must
  be retired if this RESULT ever stops being EQUAL. BH-proven; QSR
  changes integer-abs(INT32_MIN) per the simulator, so the pass gate is
  BH-only. proved 2026-08-20.  REDUCTION.md (2026-08-20) records
  the complete admitted-spelling set that reduces pointwise to this
  RESULT's value function (LE polarity; GE/GT else-forms) — reductions
  retire with the RESULT.
- ccmask-direction-complete/ — the four float order directions vs +0.0
  of the ccmask zeroing fold: SETCC/COMPC CC lowering vs the
  SFPGT/SFPLE mod1=8 SET_DEST keep-masks (LE/GT direct-operand, LT/GE
  swapped-operand writable-zero forms). EQUAL (0 mismatches per
  direction over 2^32; cut/hw stream commitments identical per
  direction). LICENSES the LT/GE arms of the rvtt_ccmask fold
  (gimple-rvtt-ccmask.cc); those arms must be retired if this RESULT
  ever stops being EQUAL. BH-only (SFPGT/SFPLE are BH_QSR; the pass
  gate is BH). proved 2026-08-20.
- sm32-cast-elision-shift/ — leftshift SM32 software-cast chain vs the
  INT32_2S_COMP conversion-in-load (cast-free) form. Amount dimension
  EQUAL (2^32); value dimension NOT-EQUAL (92,341,796,868 over 32x2^32,
  two exact closed-form classes, other=0; sound only at k=0 and k=31).
  Refusal: sm32-cast-elision-refuted.
- shft-imm-vs-reg/ — SFPSHFT dynamic-immediate form vs register form,
  per amount k in [0,31] x 2^32 values. EQUAL (0 mismatches every
  stratum). Pre-discharged obligation for a FUTURE loop-invariant
  amount-materialization (formation) mechanism on the unaryshift row;
  no rule attached here.
- int-not-allones-subtract/ — one's complement stated as (-1) - v
  (SFPIADD mod1=2SCOMP|CC_NONE with all-ones minuend) vs SFPNOT.
  EQUAL (0/2^32; streams hash-identical).  LICENSES the rvtt_int_not
  fold (-mtt-tensix-optimize-int-not, gimple-rvtt-int-not.cc); the
  fold must be retired if this RESULT ever stops being EQUAL.  Proven
  against the shared TT_VERSION<=1 simulator arm (BH+WH oracles); the
  pass gate is BH+WH, QSR fails closed.  proved 2026-08-21.
- native-compare-gtle/ — the GT and LE float compare-against-+0.0 arms
  of the BH-native SFPGT/SFPLE SET_CC (mod1=1) lowering vs the
  SETCC-composed web.  EQUAL (0 mismatches per direction over 2^32).
  ORPHANED as of 566071bf728: the subject it licenses no longer exists.
  It cites `rvtt_emit_sfpxfcmps` / `rvtt_emit_sfpxfcmpv` in rvtt.cc,
  which upstream DELETED when it folded sfpxicmps/icmpv/fcmps/fcmpv
  into the single sfpxcmp builtin and moved compare lowering into
  pass_rvtt_vif (gimple-rvtt-pred.cc expand_cmp_using_gtle,
  upstream 32022f123c4 2026-08-27, widened ff65ed1f4fe 2026-09-23).
  Consequently: `-mtt-tensix-optimize-native-compare` has NO reader
  anywhere in the backend (riscv.opt:971 defines the variable, nothing
  consumes it, so the option cannot change codegen); the RTL pattern
  the proof audits (rvtt_sfp<gtle>_cc in rvtt.md, the hard-coded
  `mod1 == 1, VC == +0.0' form) has no emitter; and the two registered
  refusal names native-compare-operand-shape /
  native-compare-target-ungated are never emitted.  DO NOT read this
  RESULT as backing the live lowering: upstream's
  expand_cmp_using_gtle admits (type >= SMAG on BH+QSR) and
  (type == INT on QSR) with BOTH compare operands arbitrary and
  LT/GE commuted into the GT/LE path, where this proof swept the
  FLOAT type only, against +0.0 only, on the BH oracle only, in the
  GT/LE directions only.  That lowering is upstream's and carries its
  own effect audit in rvtt.md; this proof does not reach it.
  proved 2026-08-25 (lane GW).
- stochrnd-store-round/ — SFPSTOCHRND(NEAREST, fp32->fp16b/fp16a) then
  SFPSTORE(BF16/FP16) vs the direct store, per float row.  NOT-EQUAL
  both rows (BF16 2,155,741,184/2^32; FP16 268,435,456/2^32; classes:
  finite round-up vs truncation, -0/denormal sign normalization,
  NaN->Inf).  Standing refusal: stochrnd-store-rounding-divergent
  (gimple-rvtt-store-fold.cc) — the explicit rounding instruction is
  semantics the store's own conversion path cannot reproduce; the
  "fold the rounding into the store" cut is never re-mined.  proved
  2026-08-21.
- store-sink-roundtrip/ — the Dst load->store round trip per format
  pair, for the predicated store-sink arm of the store-fold pass.
  (INT32,INT32) BH raw pair EQUAL over 2^32 — LICENSES the S2 sink for
  that pair only (retire if it stops being EQUAL).  BF16 (254/2^16),
  FP16 (2046/2^16), FP32 (16,777,214/2^32, all denormal-flush) and the
  WH INT32_SM pair (1/2^32: -0) are NOT-EQUAL — standing refusal
  store-sink-format-canonicalizing: an all-lanes write-back
  canonicalizes Dst, so eliding it is architecturally visible.  proved
  2026-08-21.  COVERAGE GAP (2026-09-30): the generated (SRCB, SRCB)
  sink row derives its class as the most-refusing of the three
  DIAGONAL float pairs, which assumes the load and the store resolve
  MOD0_FMT_SRCB to the SAME concrete format.  SFPLOAD.md:67-82 takes
  SrcBFmt from ThreadConfig.SFPU_DEST_FMT_Base on Blackhole when
  SFPU_DEST_FMT_Enable is set; SFPSTORE.md:58-71 has no such clause and
  records its BH SrcB behaviour as "not fully characterized", so the
  two can resolve differently from one instant of config state.  The
  row therefore quantifies over nine (load-resolution,
  store-resolution) cells and only three are swept.  `--cross' sweeps
  the other six: four are WIDTH-MISMATCH (FP32 writes a 32-bit Dst
  datum against BF16/FP16's 16-bit one) and the two same-width cells,
  (BF16, FP16) and (FP16, BF16), are NOT-EQUAL on 65530 of 65536 Dst
  bit patterns -- nowhere near the ratified denormal-flush class (254
  and 2046 on the diagonal).  The row's LICENSED/DENORMAL_FLUSH class
  is unsound over six of its nine cells.
- ccmask-eqne-zero/ — the EQ/NE float directions vs +0.0 of the ccmask
  zeroing fold (a later widening of the direction-complete family): the single-SETCC
  raw-bit lowerings (mod6 LREG_EQ0 / mod2 LREG_NE0) vs the two-compare
  SET_DEST compositions EQ keep = SFPOR(SFPGT(x,0), SFPGT(0,x)),
  NE keep = SFPAND(SFPLE(x,0), SFPLE(0,x)).  EQUAL (0 mismatches per
  direction over 2^32; cut/hw stream commitments identical).  LICENSES
  the EQ/NE arms of the rvtt_ccmask fold under
  -mtt-tensix-optimize-ccmask AND -mtt-tensix-optimize-cc-region-general
  (gimple-rvtt-ccmask.cc); retire those arms if this RESULT ever stops
  being EQUAL.  BH-only (the pass gate is BH).  proved 2026-08-31.
  The proof settles WHAT only: since 2026-09-03 the arms also pass a
  delivery-cost WHEN-gate (eqne_fold_priced_profitable_p, refusal
  ccmask-eqne-fold-unprofitable) after device round 6 measured the
  unpriced compositions as kernel-cycle regressions on every folded
  corpus row (sign +39.8%, atan2 +10.0%, remainder/fmod/trig ~+2%).
- cc-narrowing-writers/ — STRUCTURAL certificate (audit, not a value
  sweep; the obligation is decided by the simulator's enable-masked
  for_each_lane loop guard, not by operand values): the raw typed CC
  writers admitted into the audited-narrowing set by
  -mtt-tensix-optimize-cc-region-general (gimple-rvtt-invariant.cc
  cc_narrowing_modifier_p: SFPGT/SFPLE SET_CC, SFPEXEXP, SFPLZ,
  SFPIADD families) never touch a disabled lane's enable bit; SFPENCC
  and the empty-stack COMPC are recorded NOT-narrowing.  Also the
  soundness record for the tree's loop-scoped in-frame
  vocabulary-external admission
  (rvtt_cc_region_tree::loop_cc_ambient_preserving_p).  Retire those
  arms if any listed writer's pinned-simulator semantics stop visiting
  lanes through the enable-masked for_each_lane.  proved 2026-08-31.
