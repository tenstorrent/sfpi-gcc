# Counted-row parameterized replay formation — design (Lane BF, 2026-08-18)

Status: IMPLEMENTED (Lane BM, 2026-08-18) as
-mtt-tensix-optimize-counted-row-formation (default off) in
rtl-rvtt-replay.cc: a canonicalization-and-formation phase ahead of the
word-exact discovery.  Deviations from the sketch below, learned on the
real welford stream:

- Exclusion is TRANSPARENCY: exclusion-admissible members (single-slot
  immediate materializations by the audited effect classes -- never by
  instruction identity) are transparent to sequence discovery and are
  RELOCATED to the head of the clone that consumes their value (the
  hand's serial recip-delivery discipline); one with no consumer in the
  family moves out past the containing clone's tail.  Movement legality
  is value-based (uses follow the new position; no CC write or opaque
  instruction crossed), with the occupancy simulation as the final gate.
- Clone canonicalization is a whole-value REWRITE plan (definition and
  every use), with: seed chain-closure (a value carrying both a def role
  and a live-in role retargets the reference definition), read-modify-
  write tie propagation (tied-operand halves move together), reference-
  clone rotation, per-clone drops, live-in bridging by one all-lanes
  move where the pinning definition is fixed (multi-definition bundles),
  and a bystander cascade (an untouched value displaced into the
  evacuated register) -- all verified by a whole-block final-assignment
  occupancy simulation plus a lane-state constancy window (no CC write
  inside any rewritten value's span).
- The phase EMITS the capture and launches itself (mirroring
  replace_sequence) and marks its slots persistent, rather than relying
  on rediscovery.
- Refusal taxonomy (append-only, extended): counted-row-excluded-member-
  unmovable, counted-row-residual-not-uniform, counted-row-map-live-out,
  counted-row-slot-budget, counted-row-rename-interference,
  counted-row-rename-constraint, counted-row-lane-state,
  counted-row-bridge-clobber.
- Coverage on the welford perf body at this pin: one 5-member record,
  7 clones (the x2-variant rows + the trailing-delta alignment), body
  issue census 234 -> 218.  The full x-bridged single-record coverage
  (~26 rows, the ~203-slot arithmetic below) remains open: the mixed-
  alignment clone partition and reference-clone x-liveness interact
  (bridge targets must be dead across each block), documented for the
  next increment.

## The measured gap it addresses

The welford perf body (tt-metal test_sfpu_welford_prefix_snapshot, sem
impl 2) after transp-involution + epoch closure: 251 issued slots / ~267
executed SFPU ops, expected ~320 cycles vs hand 326 — a marginal win.
The hand kernel issues 80 slots + 64 pushed words for the same algorithm
(326 measured) by replaying 6-instruction row programs recorded once and
delivering per-row reciprocals from the scalar core.  A decisive win
needs the same row-program compression from the compiler.

## Why the existing compression refuses (named, dump-proven)

1. `Slots [24,+8)`: the (hand-shared) init records slots 0-23; only 8
   slots remain.  Stealing recorded-but-never-launched-in-function slots
   is UNSOUND: the replay buffer is device state and launches may live in
   code the compiler cannot see; the function-scope ownership doctrine
   does not extend to overwriting another owner's recorded content.
2. Word-exact sequence identity: the per-row bodies differ in
   (a) reciprocal immediates (26 of 32 rows carry an SFPLOADI pair
   mid-row) and (b) register assignment (the allocator rotates temp
   registers row to row and block to block).
3. Trip shape: the body is fully unrolled straight-line template code;
   the counted-loop machinery never sees a loop.

## The mechanism (generic; no operation identity anywhere)

Extend the in-block sequence discovery (rtl-rvtt-replay.cc) from
word-exact runs to PARAMETERIZED runs:

1. **Invariant-violation exclusion** ("the hand trick, derived"):
   a candidate clone set may disagree on members that are
   (a) single-word immediate materializations (SFPLOADI-class, audited
   attribute class, no CC/Dst/RWC effect) and (b) movable to the clone
   boundary within their own clone (ordinary dependence check on the
   value's consumer).  Excluded members leave the record and are issued
   between launches; the record holds the residual, which must then be
   word-exact.  Cost model: each launch grows by the excluded words
   (in-stream, 1 slot each); the recording shrinks; the existing
   recalibrated benefit formula prices it (delivered vs executed,
   rvtt-cost.md).
2. **Clone canonicalization for register rotation** (the MVE
   complement, literature scan idea 1/2 lineage): when clones are
   effect-isomorphic under an evolving register value map (the
   launch-conversion matcher ALREADY implements this exact test,
   rtl-rvtt-replay.cc "Launch conversion of isomorphic instruction
   runs"), a clone whose differing definitions are all dead after the
   run may be REWRITTEN to the recorded registers instead of merely
   matched — the launch-conversion machinery run as a transform, not
   just a recognizer.  Where a live-out register differs, insert the
   hand's own canonicalization (one SFPMOV, audited all-lanes mod) and
   price it.
3. **Slot budget honesty**: formation under budget pressure prefers the
   SHORTEST row program with the most clones (the welford row residual
   is 4 words x 4 input-register variants = 16 slots -- does not fit 8;
   a 2-variant record (8 slots, x0/x1) covering half the rows does).
   The budget only grows via a proof that user-recorded slots are dead,
   which requires whole-program knowledge we refuse to fake.

## Refusal taxonomy (append-only)

counted-row-excluded-member-unmovable, counted-row-residual-not-uniform,
counted-row-map-live-out, counted-row-slot-budget, plus the inherited
epoch/dirty refusals.

## Pre-registered arithmetic for welford (if all three land)

Residual row program 4 words x 4 variants: with 8 free slots, a
2-variant record covers rows using L0/L1 (16 of 32 rows):
per covered row 1 launch + 2 excluded loadi = 3 slots vs 6 today
= −3 x 16 = −48 issued slots → ~203 issued / 267 executed.
Execution-bound floor then dominates: predicted ~300-315 vs hand 326
(−3% to −8%).  Full coverage (needs 16 slots = the budget question)
would reach ~180 issued.
