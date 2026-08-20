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
