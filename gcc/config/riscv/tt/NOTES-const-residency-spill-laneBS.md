# Constant residency + spill mechanism (lane BS, 2026-08-18)

Four tiers, cheapest first, for the SFPU's architectural fact that the
LREG file (8 allocatable registers, riscv.h `SFPU_REG_NUM`, hard regs
80..87) has NO memory spill path: the `rvtt_sfpassign` m-alternatives
exist only so LRA constraint matching succeeds (rvtt.md, "reload blows
up" comment), and emitting one was an assembly-output ICE
(rvtt.cc `rvtt_mov_error`).

## Verified motivating facts

(a) The lane BR ICE reproduces minimally: nine loop-held constant
vFloats + one accumulator ICE the pin-10 reference cc1plus
(2911f0e680e4) at `final` with "cannot store sfpu register (register
spill)"; the real-world subject (xielu fresh body with alpha_p/alpha_n
held as loop-invariant vFloats) ICEs identically at
sfpi_funcs.h:473 (pin-control leg in
~/sfpi-uplift/laneBS-evidence-20260818/log-flagship-pincontrol.log).

(b) Production kernels hand-park constants: vConstFloatPrgm in
production xielu; the acosh/asinh Dst round-trip
(ckernel_sfpu_trigonometry.h) is an explicit in-comment workaround for
the same reload-budget family.  Correction to the briefing: the
one-line acosh clean form (log1p argument passed directly instead of
через Dst) COMPILES at pin-10 in both dest_acc legs -- the live blocked
member of that family is ASINH's documented clean form (|x| and x*x
held in vFloats + no round-trip), which ICEs at pin-10 with the same
spill signature.

(c) The landed prgm-const fire (M3) covers only the fusion-enabling
SFPADDI/SFPADD class; the pure in-loop-loadi class refused "pending its
own benefit discipline" (file comment) -- discharged here.

## Tier 1 -- rematerialization (-mtt-tensix-optimize-const-remat)

A value defined by an SFPLOADI chain from scalar-only inputs
(single `sfpxloadi`/`sfploadi`, or `sfploadi` + `sfploadi_lv`
upper-merge whose lv link is single-use) spills as nothing and reloads
as its chain: when the function-wide liveness model of allocatable
vector SSA values exceeds `SFPU_REG_NUM`, each audited use gets a fresh
clone of the chain immediately before it and the long-lived original is
deleted.  Scalar inputs live in GPRs (spillable normally), so the
reload is always available; this covers both fp32 immediates and
RUNTIME scalars (the xielu alphas -- their GPR halves are reused, only
the push pair is re-issued).

Lane-predication soundness: SFPLOADI writes only CC-enabled lanes
(craq-sim tensix.cpp:8546,8556-8568 [SIM]; specs SFPLOADI.md:37-39
[SPEC]).  A clone placed immediately before its consumer executes under
the consumer's exact CC state, so every lane the consumer reads AND
commits was just written with the constant -- PROVIDED the consumer
writes only enabled lanes and reads operand lanes lane-locally.  The
audited-consumer table in gimple-rvtt-prgm-const.cc carries per-entry
sim/spec citations (mask idiom `cc_en ? cc : ALL` + for_each_lane,
tensix.cpp:8304-8310); everything else refuses by name
(consumer-lane-discipline-unaudited).  Structural exclusions with
citations: SFPMOV mod1==2 (all-lanes copy, :9008-9010, and every plain
gimple vector copy/PHI lowers to it), SFPTRANSP (per-destination-lane
predication over cross-lane reads, :9488-9493; SFPTRANSP.md:44-45),
SFPSHFT2 family (unpredicated 32-lane source snapshots, :9997-10063),
SFPCONFIG (own Imm16 gating, not CC, :9665-9682).

The pressure model is backward SSA liveness of vector values that will
occupy allocatable LREGs (constant-register reads excluded: the
`rvtt_sfpreadlreg` expander emits a zero-cost cstlreg unspec for
indices >= `SFPU_CREG_IDX_LWM`), with per-point peaks and the
dead-def-occupies-its-point correction -- the function-wide analogue of
`rvtt_loop_lreg_pressure_legal_p`.  Only candidates live through an
over-pressure block are touched, in SSA version order, stopping at the
first model state within capacity.  Residual over-pressure dumps
lreg-pressure-unresolvable and falls through to Tier 3.

## Tier 2 -- residency (-mtt-tensix-optimize-const-residency)

Proven-constant values (full 32-bit lane image recoverable: sfpxloadi
bits 31/-32/32 verbatim, shortened SFPLOADI FLOATB imm16<<16) are
parked in free PRGM registers -- SFPCONFIG dests 12..14, the same
audited register set and LaneConfig-reset survival facts as the landed
M3 class (rvtt-mop-derive.cc:239-263: the dest-15 reset word 0x910000F1
writes LaneConfig only, LReg[11..14] writes exist solely in the VD
11..14 arm [SIM specs/SFPCONFIG.md + craq TENSIX_EXECUTE_SFPCONFIG]).
Emission is BL's existing mechanics (sfpxloadi staging + sfpwriteconfig_v
+ readlreg), sharing the claim bitmask and identical-value dedup with
the M3 class through `prgm_state`.

Two candidate classes, priced per rvtt-cost.md (delivery model,
RISC_PUSH_X100):
- LOOP: an in-loop invariant materialization saves 2 pushed words per
  iteration for 3 once => profitable at proven trips >= 2; the proof is
  a structural first-iteration exit-test evaluation (neither
  scalar-evolution nor loop_niter_by_eval is usable at this pipeline
  position -- both assert LOOPS_NORMAL/preheader state that
  AVOID_CFG_MODIFICATIONS does not establish).  Refusal:
  trip-count-unproven.
- PRESSURE: out-of-loop constants are worth parking only for the freed
  LREG => admitted only while the model exceeds the file; in-place
  reprogramming at the definition (no code motion, so no loop-admission
  obligations).  Ranking: LOOP before PRESSURE, more uses first,
  deterministic value tiebreak.  Refusals: prgm-exhausted (per
  candidate), cc-region-unproven (whole function, same all-lanes
  discipline as M3 -- SFPCONFIG asserts all lanes enabled), plus the
  shared TU freedom-proof refusals.

Cross-call composition: BQ's caller-closure API was not landed at base
60bc8dc84be; residency proofs are function-local, and the cross-call
ambient case refuses through cc-region-unproven / the TU scan by name.
When BQ lands, the ambient all-lanes proof at the programming point may
use the caller closure -- extension point `function_writes_cc_p`.

## Tier 3 -- named diagnosis (rtl-rvtt-spill-diag.cc, unconditional)

A new RTL pass directly after allocation (first before pass_postreload)
turns every allocated XTT32SI memory move (INSN_CODE ==
rvtt_sfpassign with a MEM operand; no other XTT pattern accepts a
vector memory operand) into `error_at` the insn's source location:
"SFPU vector register pressure exceeds the 8-register LREG file ...
(lreg-pressure-exceeded)", with a note naming both relief flags.
`rvtt_mov_error` loses ATTRIBUTE_NORETURN and stands down after
`seen_error ()` (no object file is produced after errors); on an
error-free stream it remains the ICE backstop for true compiler bugs.
Reachability: every spill path flows through rvtt_sfpassign's m
alternatives (the only vector-memory pattern), and the pass gate is
plain TARGET_XTT_TENSIX -- no flag, no pressure precondition -- so the
old ICE is unreachable except through a compiler bug, which is exactly
what internal_error is for.  The "maximum number of generated reload
insns" LRA-cycling variant mentioned in the asinh comment did not
reproduce on any current subject (the pin ICEs with the spill
signature); if it resurfaces it is upstream LRA cycling and needs its
own repro.

## Tier 4 -- true Dst spill (design only; nothing implemented)

For over-pressure that tiers 1-2 cannot relieve (computed values, e.g.
asinh's clean form: 16 constant remats fired, residual peak stayed 10 --
`a`, `x2`, sqrt/recip temps are not loadi chains), the correct next
mechanism is what production code does by hand: park a computed value
in a PROVEN free Dst region (SFPSTORE at spill, SFPLOAD at reload).
Requirements, refusing by default:
- a kernel Dst-usage census within the TU proving a free region: every
  dst_reg index expression the TU can execute, over-approximated
  through the same TU-scan machinery as the PRGM freedom proof (raw
  SFPLOAD/SFPSTORE words decode through the audited table; typed
  accesses carry their index expressions); NEVER an assumed scratch
  address;
- Dst addressing state discipline: the spill/reload pair must prove the
  RWC/ADDR_MOD state at both points addresses the same row (the
  dst-ownership pass's boundary machinery is the natural host), and
  fp32/bf16 layout agreement (ALU_ACC_CTRL reaching-definition class);
- pricing: SFPSTORE+SFPLOAD = 2 issue slots + result latency, against
  remat's 2 pushed words -- Dst spill wins only for values whose
  recompute chain exceeds ~2 slots or is impossible; BF's
  transp-involution park machinery already prices Dst parks and is the
  shared cost home;
- CC discipline: SFPSTORE/SFPLOAD are lane-predicated (tensix.cpp:8610,
  :8439) with the same adjacency argument as Tier 1, plus the
  SFPLOAD mod0==10 all-lanes exception to refuse.
A cheaper sibling worth implementing first: RECOMPUTE remat of pure
lane-local single ops (abs/mul from still-live operands) -- it would
close asinh's clean form without touching Dst; operand-liveness-at-use
bookkeeping is the only new obligation.

## Gate summary (evidence ~/sfpi-uplift/laneBS-evidence-20260818/)

- corpus flags-off byte-identity, 270-node shared-farm session,
  base-60bc8dc-vs-edited cc1plus (-B hybrid method);
- shim-farm OFF vs ON changed-row inventory + CRAQ (pinned BH sim
  32489dda) on every changed row;
- full rvtt.exp FAIL set == frozen-14, 2911 PASS (new families
  const-remat*, const-residency*, spill-diag* all green; prgm-const*
  and neighbors unchanged);
- flagship: xielu loop-held body and acosh clean form COMPILE and are
  CRAQ-green under the new flags; asinh clean form refuses by name
  (computed-value class, Tier 4).
