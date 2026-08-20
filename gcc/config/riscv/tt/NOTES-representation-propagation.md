# Representation propagation (lane CN) — design notes

Status: design + default-off prototype (`-mtt-tensix-repr-prop`).
Motivating case: mulint32 fresh row (+51.6 vs hand); family: every kernel that
pays conversion instructions between redundant value representations at
contract boundaries (SM32 <-> two's-complement int, and by the same math the
IEEE two-zeros class behind the eqz -0.0 record).

## 1. Problem statement

Typed kernels state a *representation contract* on their Dst traffic in the
type system (sfpi `DataLayout`): the harness writes and reads Dst rows in a
declared representation, and the kernel converts at the boundary.  On BH the
`DataLayout::SM32` accessors lower to a raw `SFPLOAD` (mod0 = FMT_INT32)
plus an explicit `SFPCAST` (mod1 = 3), and symmetrically on the store side
(sfpi_funcs.h vReg_ operator vInt / operator=(vInt); the in-load conversion
mode `INT32_2S_COMP` is architecturally inert on BH — lane CI's audit — so
sign-magnitude-in-Dst is *convention*, not hardware).  Each such conversion
occupies an issue slot and a row member; on the mulint32 fresh row 3 of the
17 members are conversions (a-cast, b-cast, res-cast).

Sometimes those conversions are unobservable: the producer and every consumer
of a value web agree on the representation, or the conversions cancel across
the web.  A clean compiler pass can prove that per web and delete them.  The
decision inputs must be charter-clean (§0.3): typed insn identities and
constant mod immediates checked against an audited capability table — never
operation names, opcode calendars, or value fingerprints.

## 2. Semantic foundation

### 2.1 Conversions as lanewise bit maps

A conversion instruction applies a pure lanewise map `c : bits32 -> bits32`.
The pass reasons about *bit-exactness*, never value plausibility: an elision
is licensed only if the final bit pattern at every observable boundary is
unchanged for **all** inputs.  Value-level reasoning (two bit patterns that
denote the same abstract value) is only sound at a boundary whose observer is
value-typed, and observer typing is an owner contract, not a compiler
inference — so the default is bit-level, fail closed (§6, §8.4).

### 2.2 The audited BH fact: SFPCAST mod1=3 is a bit involution

sfpi_constants.h (219-220) and the sfpi_lib.h smag_to_int/int_to_smag
comments record the audited semantics: on BH the int<->int cast encoding
mod1=3 is a *self-inverse sign-preserving conditional negate* — one encoding
implements both SM32->2C and 2C->SM32, and `c(c(x)) = x` for every 32-bit
pattern.  The ISA-defined corner (SM32 -0 <-> most-negative int32,
0x80000000) is a fixed point of the involution, so it does not weaken pair
cancellation; it only breaks *value*-level identities (see §8.4).  QSR has
directional encodings (mod1=2 / mod1=3 are mutual inverses); WH has no int
cast at all (sfpi lowers SM32 conversion to a predicated negate — the pass
sees no conversion insn and is inert there).

These facts live in an audited table (arch, insn class, mod immediate) ->
(conversion kind, direction, involution?) with citations.  A conversion whose
(arch, mod) has no row is refused: `repr-conversion-unaudited`.

### 2.3 Choose-transparency

Pair cancellation composes through any node that lanewise *chooses one
input's bits unmodified*: SSA PHI joins, `COND_EXPR` selects with a non-web
condition, plain copies, and predicated live-value merges
(`sfpassign_lv`), regardless of the (hidden, arbitrary) lane predicate —
because for any lanewise map `c` and any lanewise selector `sel`,
`c(sel(x, y)) = sel(c(x), c(y))`.  This is a structural property of choose
nodes, valid for arbitrary `c`; it requires no commutation audit per
operation.  Every *arithmetic or logic* operation is NOT choose-transparent
(a multiply, add, shift, or compare on converted bits observes the
representation), and the pass refuses such consumers by name.  This is
exactly why mulint32 refuses (§6).

### 2.4 The cancellation theorem (what the prototype implements)

Let W be a set of SSA values closed as follows:
- *sources*: results of conversion calls whose (arch, mod) table row is an
  involution (or whose direction is D);
- *interior*: PHI nodes and audited choose insns ALL of whose data operands
  are in W;
- *sinks*: conversion calls consuming a W value whose table row is the same
  involution (or direction inverse-of-D, same kind).

If (a) every use of every W value is an interior node, a sink, or dead, and
(b) every interior operand edge enters W through a source (no foreign leaf),
then rewriting the web — replacing each source by its own (raw) input,
keeping interior nodes, and replacing each sink's result by the raw web
value — leaves every sink output bit-identical for all inputs:
each lane's path is source -> chooses -> sink, i.e. `c^-1(c(x)) = x`.
Everything outside the web (including the raw load feeding the source and
the store consuming the sink) is untouched, so no other observable changes.
Obligation (a) failing is `repr-web-consumer-not-transparent`; (b) failing is
`repr-web-leaf-unproven` (e.g. an sfpnovalue or constant entering a merge).

Contract boundaries need no special-casing in this formulation: a store,
call, return, volatile access or asm consuming a *converted* value is simply
a non-transparent consumer of that value — if it consumes the sink's result
it is outside the web and unaffected; if it consumes an interior raw value
directly, obligation (a) fails and the web refuses.  Fail-closed by
construction.

## 3. Decision-input audit (charter §0.3)

| decision input | source | why clean |
|---|---|---|
| "this call is a conversion" | rvtt insn identity (`sfpcast` insn id from the target's insn table) | typed insn semantics, not name matching on user code |
| conversion kind/direction/involution | audited table keyed (arch, insn class, constant mod immediate) | raw encodings' legitimate home = capability tables; every row carries a citation (ISA doc + sfpi_constants.h + craq-sim executor) |
| choose-transparency | structural node kind (SSA PHI) + audited table flag on the insn id (`sfpassign_lv`) | a structural lanewise-selection property, proven once in §2.3 for arbitrary maps |
| web membership / observers | SSA def-use closure | dataflow, no shapes |

No operation names, no opcode calendars, no coefficient fingerprints, no row
lengths.  Refusals are named and byte-identical (the pass mutates nothing on
any refusal path).

## 4. Refusal taxonomy

- `repr-conversion-unaudited` — conversion call whose (arch, mod) has no
  audited row, or whose mod operand is not a compile-time constant.
- `repr-web-consumer-not-transparent` — a web value is consumed by an insn
  that is neither an audited choose nor a matching sink (arithmetic, logic,
  compare, store, call, ...).  The mulint32 SM32 web refuses here.
- `repr-web-leaf-unproven` — a choose/PHI operand enters the web without
  passing through a source conversion (uninitialized `sfpnovalue`,
  constants, values of another conversion kind).
- `repr-web-kind-mismatch` — source and sink conversions exist but their
  table rows do not compose to identity (different kinds, or directional
  rows composed same-direction).
- (design, not in prototype) `repr-boundary-not-value-typed` — a candidate
  elision that is only value-exact, not bit-exact (redundant-representation
  canonicalization, §8.4), refused unless the owner declares the boundary
  value-typed.

## 5. Placement and mechanics

Gimple SSA pass, default off, after sfpi inlining and before the rvtt
combine/scheduling stack, so the downstream pipeline (including the macro
planner's region discovery and WP12 derivation) sees the shorter body — the
whole point is that removed conversions shrink *row membership*, not just
instruction count.  The rewrite is: for each proven web, redirect sink-result
uses to the raw value chain and delete the conversion calls (standard SSA
replace + DCE of the dead casts).  No insn is mutated on refusal paths.

## 6. mulint32 adjudication

Facts inherited (lane CI, evidence 20260819): fresh V0 SM32 row = 17 members
(2 loads, 10 simple incl. 3 conversions, 4 mad, 1 store), formed ii=12 + 1
interlock NOP = 13 words/row, ~460 issue slots/tile ~ 57.6 cy/tile; hand raw
kernel 8 words/row ~ 285 slots ~ 35.6 cy/tile; WP12 visibility deadline
consumer_slot >= last-raw-operand-reader + 4; V0's copy is load-bearing.
`mulint32-raw-contract-owner-decision`: dropping the conversions weakens the
stated golden contract to the hand kernel's non-negative domain.

Under the **current SM32 contract** this pass correctly refuses the fresh
mulint32 web: the conversion results are consumed by the radix-23 multiply
chain (`fractional_mul` MUL24s, shifts, adds) — none choose-transparent —
so the web refuses `repr-web-consumer-not-transparent` and the bytes are
identical.  This is not a limitation to engineer around: sm<->2c conversion
does not commute with wrap multiplication, so *no sound compiler mechanism
can remove these conversions while the boundary is bit-observed in SM32*.
The cost is attributable entirely to the contract.

Under the **owner-pending raw-U32 contract** there are two equivalent
delivery shapes:
1. Typed source change: the fresh body spells `DataLayout::I32` instead of
   `SM32` (3 sites).  The conversions never exist; no pass needed.
2. Keep the SM32 spelling, add an owner-approved domain assertion
   (non-negative inputs and product => the conversion is the identity map on
   the reachable domain); the pass elides conversions proven identity-on-
   domain.  Same emitted code; requires assertion plumbing (not in the
   prototype).

Either way the recovered cycles are a property of the raw row, measured
empirically in §7 — and the measurement REFUTES the hoped-for recovery.

## 7. The raw-row oracle (MEASURED, lane CN 2026-08-19)

Two oracle surfaces, agreeing exactly.  (a) dg builtin surface: the WP12
derived-intmul-row twin compiled as-is (SM32) and with the 3 conversions
dropped (raw).  (b) The REAL fresh-kernel TU: worktree tt-metal-laneCN @
e637598767, `perf_...fresh_cpp_mul_int[...impl:1]` compiled through the
harness with the reviewed 22-flag ON set, compiler = pin-14 seed
e0754714a5b + this pass (flag off), planner dumps read from RUNNER_TEMP.

SM32 baseline TU: `region rows=32 row-len=17 runs=4`, candidates ii=12
(2-launch) and ii=12 (3-launch), **formed ii=12 launches=3 drain-elided**
— reproduces lane CI's V0 numbers exactly (79-instruction function; 26-slot
2-row record + 15 replays + config prefix ≈ 452-460 slots/tile ≈ 57.6
cy/tile measured basis).

Raw-contract TU (`DataLayout::I32` at the 3 sites): `region rows=32
row-len=13 runs=4` — the 3 conversions AND V0's load-bearing copy vanish
(17 → 13 members).  The scheduler PRESENTS ii=10 (2-launch,
launched-events=3) and ii=11 (3-launch) candidates — so the WP12 visibility
deadline is NOT the binding floor on the raw row; the slack the conversions
consumed is real.  But BOTH candidates refuse derivation:
`sequence-encoding-unproven` (schedule) then `descriptor-program-unproven`
(derive), and the row FALLS BACK to the unformed replay stream: TTREPLAY
len=15 x32 (one 15-slot row per tile row, no descriptor config).

Slot model (same accounting as lane CI):
- SM32 formed:   ~452 slots/tile  = 57.6 cy/tile  (+61.7% vs hand 35.6)
- raw unformed:  32x15 + 9 ≈ 489 slots/tile ≈ **62.3 cy/tile modeled — a
  REGRESSION of ~4.7 cy/tile vs the SM32 V0**
- raw formed ii=10/11 (hypothetical, requires new derivation vocabulary):
  26 + 16x(2x11..12) + 10 ≈ 388..420 slots ≈ 49.4..53.5 cy/tile — would
  recover only 4..8 of the 22 cy/tile gap to hand.

WHY formation collapses: the conversions were load-bearing for the
derivation, exactly as the WP12 twin's own prose records — the two in-place
operand casts are the hosted launch-VD chain events on the load carriers,
and the result cast is the launchable store-producer (the store-producer
must be a LAUNCHED event; lane CC fact).  Remove them and launched-events
drops 6 → 3: the loads have nothing to host and the store's producer is the
final SFPIADD, which cannot take the launched role (VD-tie, lane CC
falsification fact 2).  The floor MOVED from the deadline to derivation
vocabulary.

**Quantified verdict for `mulint32-raw-contract-owner-decision`:**
1. Under the SM32 contract the pass refuses (correct, bit-observable web);
   V0 formed ii=12 stands (lane CI's optimality proof unchanged).
2. The raw-U32 contract change ALONE is a net LOSS today: modeled ~62.3 vs
   57.6 cy/tile, while also weakening the golden contract (CL framework
   point 5 — and here the weaker-contract arm doesn't even win).
3. The contract change becomes profitable ONLY combined with new derivation
   vocabulary (a launchable non-conversion store-producer and sequence
   encoding for low-launched-event rows), ceiling ~49-54 cy/tile — closing
   at most ~a third of the gap to hand 35.6.
Recommendation: KEEP the SM32-typed fresh body; close the owner decision as
refused-on-measurement; re-open only alongside the scheduler follow-ups CI
pre-registered.

## 7b. Contract-adjudication discipline inherited from lane CL (eqz memo)

Lane CL's DECISION-eqz-negzero.md §Generalization — OWNER-SIGNED precedent
for contract-boundary reasoning — governs this class.  Instantiated for mulint32: (1) the operative golden is the
framework-level signed int32 wrap product — negative operands are IN the
shipped contract; (2) hand-leg identity must be verified against the
archived ELF's runtime dispatch, not symbol presence (CI's numbers carry
that provenance; the production row is an ablation pair, quoted as such);
(3) reachability is pipeline-dependent and must be checked per route: the
mul_int row drives Int32->Int32 through the 32-BIT unpack-to-dest path,
where negative values (unlike the 16-bit path's sign-of-zero) SURVIVE to
Dst — so the raw weakening is observable on the row's own pipeline and any
future raw adoption needs an executed golden-edge divergence twin
(negative-operand lanes) per CL's Option-B discipline; (4) both rulings
priced as code (this oracle); (5) prefer same-contract A/B arms — moot
here, the weaker arm loses anyway.

## 8. Failure modes and non-goals

### 8.1 Predication / live-value forms
`sfpassign_lv` is the audited choose and is admitted with both data operands
in-web.  Any *other* LV-form insn in a web is a computation, not a choose —
it falls under `repr-web-consumer-not-transparent` automatically (it is not
in the choose table).  Deleting a conversion never changes the lane-write
sets of remaining insns: the web rewrite only re-routes SSA values.

### 8.2 Cross-function webs
Calls are opaque consumers (not in the choose table) — webs touching them
refuse.  No IPA in the prototype.

### 8.3 Store/reload roundtrips through Dst
A converted store later reloaded and converted back (spill-like use of Dst)
is a real cancellation candidate but requires an ownership proof that no
other reader observes the row between store and reload (planner-style region
ownership).  Designed extension; the prototype's webs are SSA-only, so these
refuse naturally (the store is a non-transparent consumer).

### 8.4 Redundant representations and canonicalizing maps
SM32 has two zeros; IEEE FP32 has two zeros.  A conversion pair on such a
representation may be *value*-exact but not bit-exact (canonicalization:
e.g. FP32 SFPABS maps -0.0 to +0.0; a QSR-style directional sm->2c followed
by 2c->sm maps sm -0 to +0).  The BH involution avoids this (fixed point),
but the general table must distinguish `involution` (bit-exact pair) from
`inverse-pair-modulo-kernel` (value-exact only), and the latter may only be
elided across a boundary the owner has typed as value-observed.  This is
exactly the eqz -0.0 class (harness injects -0.0; the raw-bit SFPSETCC zero
test forces the fresh body's SFPABS; the hand kernel's raw-u16 compare is a
narrower contract): the SFPABS is a canonicalizer whose elision is an owner
contract decision, not a compiler proof.  The pass gives that decision a
principled home but never makes it.

### 8.5 WH / QSR
WH: no conversion insn (predicated-negate lowering) — pass inert by
construction.  QSR: directional rows compose; untested beyond the table
shape, refuses without audited rows.

## 9. Relation to the WP12 deadline arithmetic

Removing conversions does not change the deadline *rule* (consumer_slot >=
last-raw-operand-reader + 4 is a property of the derivation core), but it
changes which insns are the raw-operand readers and how many explicit slots
exist between them and their consumers.  MEASURED ANSWER (§7): on the raw
row the scheduler presents ii=10 — the deadline arithmetic DOES relax when
the cooking casts and their WAR chains vanish — but the candidates die at
derivation (`sequence-encoding-unproven`/`descriptor-program-unproven`), so
the deadline is no longer the binding floor; derivation vocabulary is.
(Lane CC's falsification of the pre-registered ii=11 was the cautionary
precedent for trusting hand arithmetic here; the pipeline was the oracle.)

## 10. Prototype scope and gates

- Flag `-mtt-tensix-repr-prop`, default off; no ON-set membership proposed.
- Fire twins: SM32 predicated-select kernels (choose web), + renamed-
  equivalent + varied constants (different rows/addresses, non-special
  mantissas) + near-miss twins for each refusal name.
- Refuse twins: mulint32-shaped arithmetic consumer (byte-identical asm,
  named refusal in dump); foreign-leaf merge; unaudited mod.
- Gates: focused dg; full rvtt.exp FAIL set == frozen-9 byte-identical
  (stockcfg recipe); corpus flags-off byte-identity vs the e0754714a5b base
  build; CRAQ obligations only if any ON-inventory artifact changes (none
  expected: default-off).
