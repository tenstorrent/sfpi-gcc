# tt/proofs — exhaustive denotational proof artifacts (proposal P2)

Each subdirectory carries the proof obligation record for one proposed
proof-carrying peephole: the harness (host C, oracle semantics lifted
verbatim from the pinned craq-sim with file:line provenance), the swept
result with SHA256 stream commitments, and the matched cut's gimple.
A rule may ship in rvtt.gc ONLY citing a directory here whose RESULT is
EQUAL over the full input space; a NOT-EQUAL result is a standing named
refusal (see the NOTES-*-refusal-*.md records) so the cut is never
re-mined.

- cast-fp16a-rne/ — castfp32tofp16a software-RNE cut vs SFP_STOCH_RND
  mod1=0 rnd=0. NOT-EQUAL (33,810,429/2^32). Refusal:
  cast-cut-equivalence-refuted. laneCT 2026-08-20.
- int-abs-negate-select/ — conditional-negate CC region (v_if (v<0)
  r=0-v) vs SFPABS mod1=0 integer. EQUAL (0/2^32, INT32_MIN included;
  streams hash-identical). LICENSES the rvtt_int_abs fold
  (-mtt-tensix-optimize-int-abs, gimple-rvtt-int-abs.cc); the fold must
  be retired if this RESULT ever stops being EQUAL. BH-proven; QSR
  changes integer-abs(INT32_MIN) per the simulator, so the pass gate is
  BH-only. laneCU 2026-08-20.  REDUCTION.md (laneDN 2026-08-20) records
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
  gate is BH). laneDN 2026-08-20.
- sm32-cast-elision-shift/ — leftshift SM32 software-cast chain vs the
  INT32_2S_COMP conversion-in-load (cast-free) form. Amount dimension
  EQUAL (2^32); value dimension NOT-EQUAL (92,341,796,868 over 32x2^32,
  two exact closed-form classes, other=0; sound only at k=0 and k=31).
  Refusal: sm32-cast-elision-refuted. laneCU 2026-08-20
  (NOTES-sm32-cast-elision-refusal-laneCU.md).
- shft-imm-vs-reg/ — SFPSHFT dynamic-immediate form vs register form,
  per amount k in [0,31] x 2^32 values. EQUAL (0 mismatches every
  stratum). Pre-discharged obligation for a FUTURE loop-invariant
  amount-materialization (formation) mechanism on the unaryshift row;
  no rule attached here (NOTES-unaryshift-adjudication-laneCU.md).
  laneCU 2026-08-20.
- int-not-allones-subtract/ — one's complement stated as (-1) - v
  (SFPIADD mod1=2SCOMP|CC_NONE with all-ones minuend) vs SFPNOT.
  EQUAL (0/2^32; streams hash-identical).  LICENSES the rvtt_int_not
  fold (-mtt-tensix-optimize-int-not, gimple-rvtt-int-not.cc); the
  fold must be retired if this RESULT ever stops being EQUAL.  Proven
  against the shared TT_VERSION<=1 simulator arm (BH+WH oracles); the
  pass gate is BH+WH, QSR fails closed.  laneEK 2026-08-21.
- stochrnd-store-round/ — SFPSTOCHRND(NEAREST, fp32->fp16b/fp16a) then
  SFPSTORE(BF16/FP16) vs the direct store, per float row.  NOT-EQUAL
  both rows (BF16 2,155,741,184/2^32; FP16 268,435,456/2^32; classes:
  finite round-up vs truncation, -0/denormal sign normalization,
  NaN->Inf).  Standing refusal: stochrnd-store-rounding-divergent
  (gimple-rvtt-store-fold.cc) — the explicit rounding instruction is
  semantics the store's own conversion path cannot reproduce; the
  "fold the rounding into the store" cut is never re-mined.  laneEK
  2026-08-21.
- store-sink-roundtrip/ — the Dst load->store round trip per format
  pair, for the predicated store-sink arm of the store-fold pass.
  (INT32,INT32) BH raw pair EQUAL over 2^32 — LICENSES the S2 sink for
  that pair only (retire if it stops being EQUAL).  BF16 (254/2^16),
  FP16 (2046/2^16), FP32 (16,777,214/2^32, all denormal-flush) and the
  WH INT32_SM pair (1/2^32: -0) are NOT-EQUAL — standing refusal
  store-sink-format-canonicalizing: an all-lanes write-back
  canonicalizes Dst, so eliding it is architecturally visible.  laneEK
  2026-08-21.
- ccmask-eqne-zero/ — the EQ/NE float directions vs +0.0 of the ccmask
  zeroing fold (FABLE_GOES_BURR R2 widening 2): the single-SETCC
  raw-bit lowerings (mod6 LREG_EQ0 / mod2 LREG_NE0) vs the two-compare
  SET_DEST compositions EQ keep = SFPOR(SFPGT(x,0), SFPGT(0,x)),
  NE keep = SFPAND(SFPLE(x,0), SFPLE(0,x)).  EQUAL (0 mismatches per
  direction over 2^32; cut/hw stream commitments identical).  LICENSES
  the EQ/NE arms of the rvtt_ccmask fold under
  -mtt-tensix-optimize-ccmask AND -mtt-tensix-optimize-cc-region-general
  (gimple-rvtt-ccmask.cc); retire those arms if this RESULT ever stops
  being EQUAL.  BH-only (the pass gate is BH).  laneKL 2026-08-31.
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
  lanes through the enable-masked for_each_lane.  laneKL 2026-08-31.
