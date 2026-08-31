# FABLE_GOES_BURR — Implementation Plan for the Compiler-Audit Roadmap

Status: planning document, laneJP, 2026-08-31.
Baseline: pin-48 — sfpi-gcc `6af4fb42f9b` ("riscv-tt: make the SFPU pressure
scheduler debug-transparent (IP-6)"), cc1plus manifest `93f973d9dd94`.
Inputs: `COMPILER-AUDIT-REPORT.md` and the eight family audits
(`AUDIT-cost-model-refusals.md`, `AUDIT-licensed-folds.md`,
`AUDIT-interprocedural.md`, `AUDIT-addressing-delivery.md`,
`AUDIT-scheduling.md`, `AUDIT-regalloc.md`, `AUDIT-replay-formation.md`,
`AUDIT-constants-residency.md`), which carry the file:line evidence per
finding; the pin-48 source under `gcc/config/riscv/tt/`; and the corpus
PIN HISTORY in `tests/corpus/sweep_2x2.conf` for the certified floors this
plan attacks (history entries 42, 44, 45).

This document is the executable plan for the audit's roadmap: all 15
numbered items, the 3 board-residual attacks, and the 6-step upstreaming
strategy. Every item states its design, its compatibility contract, its
gates and proof obligations, its effort and risk, its dependencies, and
the single measurable statement that closes it. Implementation lanes
should be able to execute against this without re-deriving design.

---

## 0. CONVENTIONS

### 0.1 Compatibility classes

Every item is classified as exactly one of:

- **CLASS-I (byte-identical-everywhere refactor).** The generated code for
  the full corpus must not move in ANY flag state: corpus census
  3300/3300 x3 (OFF / TRUE-DEFAULT / ON-36) byte-identical, dg testsuite
  PASS count monotone with the frozen-16 FAIL set line-identical, dg
  ERROR count 0. No new flags; no silicon owed. Items: **#1, #3, #10,
  #11, #12, #13-stage-A, #15** (and the stage-A halves of #14 and #4).
  Where a CLASS-I item replaces a computation, the replacement must be
  proven *verdict-identical*, not merely plausible: run old and new
  engines side by side in a checking build and assert equality over the
  corpus before deleting the old one (the lp_solve cross-check pattern,
  `rvtt-bnb.cc:39-44`).
- **CLASS-B (behavior-bearing).** New machine behavior behind a new
  `Init(0)` flag (or a widened existing flag), full ceremony:
  - R9 transform-stage witness seeded on a named production row;
  - fire/refuse twin pairs including a *generality twin* (renamed
    function, varied constants, reversed/disjoint structure) with a
    FUTURE-VERDICT oracle, and a *near-miss* twin that must keep
    refusing by name;
  - ON-delta adjudication: every changed TU at ON-36 dump-attributed to
    the mechanism, paired CRAQ green on the changed set;
  - silicon before any board booking, same-leg discipline (the laneIO
    lesson: scan bookings bind to the composition-leg convention);
  - byte-identical-off proven (flag absent ⇒ stream identical to
    baseline, including the named standing refusal where one exists).

Items may be staged: a CLASS-I infrastructure stage that must not move a
byte, then a CLASS-B enablement stage behind a flag. The stages are
separate lanes and separate pins.

### 0.2 Effort classes

As in the audit: **S** = days, **M** = 1–2 weeks, **L** = multi-week,
**XL** = a campaign. This plan keeps the audit's classes; deviations are
justified in-item from the source.

### 0.3 Refusal naming

New refusal names follow the existing style: lowercase hyphenated,
mechanism-prefixed, append-only, dump-stable (e.g.
`crossloop-pressure`, `record-hoist-peel-body-foreign-insn`). Items that
land after #1 must register names in `tt/rvtt-refusals.def` (created by
#1); items that land before it use the current per-pass convention and
are migrated by #1's sweep.

### 0.4 Citation form

Source citations are `file:line` relative to `gcc/config/riscv/tt/`
unless prefixed, at pin-48. Audit citations are by audit file and
section. Line numbers will drift as waves land; the audits are the
frozen reference.

---

## 1. QUICK STRUCTURAL WINS (roadmap items #1–#4)

### Item #1 — Refusal registry + one emission helper → `-fopt-info`

**DESIGN.** The unanimous item — named in all eight audits
(AUDIT-cost-model-refusals.md §1.6 is the anchor; also
AUDIT-licensed-folds.md impr.1, AUDIT-scheduling.md impr.3,
AUDIT-replay-formation.md impr.3, AUDIT-addressing-delivery.md impr.2,
AUDIT-interprocedural.md impr.2, AUDIT-regalloc.md notes,
AUDIT-constants-residency.md notes).

Mechanism:

1. **`tt/rvtt-refusals.def`** — one X-macro table:
   `RVTT_REFUSAL (ENUM_ID, "name-string", "one-line contract")`. Seeded
   from a mechanical census of the ~370 hyphenated names in the current
   `fprintf` sites (the laneJC-style catalogue re-derivation, done once
   more, this time *into* the source of truth). Uniqueness becomes a
   compile-time fact (duplicate enum = build break); a build-time check
   greps the tree for hyphenated refusal literals not present in the
   table (append-only enforcement).
2. **`tt/rvtt-refuse.{h,cc}`** — one emission helper family:
   `rvtt_refuse (enum rvtt_refusal, location_or_null, "detail fmt", ...)`
   with overloads taking `gimple *` and `rtx_insn *` for location
   derivation. Emission goes through
   `dump_printf_loc (MSG_MISSED_OPTIMIZATION, ...)` when
   `dump_enabled_p ()`, so `-fopt-info-missed` works for the first time,
   AND through the legacy `fprintf (dump_file, ...)` stream with the
   *byte-identical current message spelling* so the ~1,088-test
   Tcl-regex suite and the board's dump-mining tooling keep working
   unmodified. The registry stores the exact current message prefix per
   pass family so the dual emission is spelling-stable.
   Per-name fire counters are kept and printed in the pass summary
   (extends the existing `n_forwarded`/`n_sunk` counter idiom,
   `gimple-rvtt-store-fold.cc:216-219`).
3. **Licensed-refusal variant**: `rvtt_refuse_licensed (enum, token_opt,
   ...)` that mechanically enforces the two invariants the licensing
   wall currently maintains per-site by convention
   (AUDIT-licensed-folds.md §1.6): token absent ⇒ emit the *standing
   named refusal* and change nothing; token present but proof-class
   out of scope ⇒ refuse regardless (the WH INT32_SM pattern,
   `gimple-rvtt-store-fold.cc:597-603`).
4. **Generated catalogue**: a build rule in `tt/t-riscv-tt` (same
   pattern as the `genrvtt-combine` → `rvtt-combine.inc` rule) emits a
   `rvtt-refusals.txt` catalogue from the .def — the 87-reason refusal
   catalogue becomes generated, never re-derived.
5. **Migration**: mechanical, family-by-family (eight sweeps matching
   the eight audit families), each sweep replacing the private
   `refuse()`/`*_refuse()` helpers (~40 of them; 760 `fprintf (dump`
   sites counted at pin-48) with registry calls. `native-compare`
   (currently the only zero-telemetry unit,
   AUDIT-interprocedural.md §native-compare) gains named refusals in the
   same sweep: `native-compare-target-ungated`,
   `native-compare-operand-shape`.

What it replaces: 760 raw fprintf sites, ~370 free-string names, ~40
private helpers, string-pointer refusal identity
(`gimple-rvtt-reassoc.cc:387-391,644`). What it wraps: the existing dump
streams (kept byte-stable).

**COMPATIBILITY.** CLASS-I. Zero behavior change; generated code
untouched by construction (dump-only). The dump *text* must also stay
byte-stable per the dual-emission rule above — the testsuite is the
enforcement.

**GATES + PROOFS.** Corpus 3300/3300 x3 byte-identical. Full rvtt.exp
run with zero regressions, FAIL set frozen-16 line-identical. New
self-tests: (a) a dg test asserting `-fopt-info-missed=optimized` (or
the chosen opt-info group) surfaces a known refusal; (b) a build-time
duplicate-name check; (c) a catalogue-completeness check (every
`rvtt_refuse` call's enum exists — free by compilation). Touches every
existing named refusal (spelling must not change); introduces the two
native-compare names above. No silicon.

**EFFORT + RISK.** S–M (audit's class). Top failure mode: a silent
message-spelling drift breaking Tcl regexes or the board's dump miners —
mitigated by the dual-emission rule and by running the full testsuite
per family sweep, not once at the end. Rollback: revert the sweep
commit(s); the .def and helper are additive and inert if unconsumed.

**DEPENDENCIES.** None. Everything downstream soft-depends on it (new
items register names instead of inventing conventions).

**ACCEPTANCE.** `grep -c 'fprintf (dump' tt/*.cc` reaches 0 for refusal
emission (residual non-refusal debug prints allowed but counted);
`-fopt-info-missed` surfaces refusals corpus-wide; the refusal catalogue
is a build artifact; corpus byte-identical x3.

---

### Item #2 — Replace bounded trip simulation with loop-iv / SCEV

**DESIGN.** (AUDIT-replay-formation.md impr.1 + §1.4;
AUDIT-addressing-delivery.md §mop-form.)

Classical formulation: RTL induction-variable analysis
(`loop-iv.cc`: `get_simple_loop_desc`, `iv_number_of_iterations`) and
GIMPLE scalar evolution (`number_of_latch_executions`) — symbolic niter
computation over affine IVs, already linked into every RTL/GIMPLE pass
and already initialized by the `loop_optimizer_init` calls these passes
make (`rtl-rvtt-replay.cc:7707`).

Consumers to convert, in order:

1. `provable_constant_trips` (`rtl-rvtt-replay.cc:1332`) — today a
   single-pattern `reg = reg + const` matcher plus brute-force
   simulation to `TRIP_BOUND = 2^16` (`:1447-1465`).
2. Its declared-policy duplicate in mop-form
   (`rtl-rvtt-mop-form.cc:319-504`) — deleted in favor of one shared
   entry point (this also discharges half of item #12's "same audited
   quantity" duplication).
3. `constant_reaching_value` (`rtl-rvtt-replay.cc:1265`) — replaced by
   DF def-use chains where the niter path doesn't subsume it.
4. GIMPLE side: `gimple-rvtt-replay-unroll.cc`'s bounded forward
   evaluation → `number_of_latch_executions` (the audit's one deduction
   from its A−, §1.12).

Structure: a shared `rvtt_loop_trips (loop, &trips)` facade in a new
`tt/rvtt-trips.{h,cc}` returning {PROVEN(n), UNPROVEN} — fail-closed,
never a guess. **Dual-oracle phase**: for one pin cycle the facade runs
BOTH the classical analysis and the legacy bounded simulator; where both
prove, verdicts must be equal or the facade refuses by the new name
`trip-oracle-divergence` (and the discrepancy is a P1). Wrap-around
counters — the reason the forward evaluation was defensible
(AUDIT-replay-formation.md §1.4) — are the class to watch: `loop-iv.cc`
models modular arithmetic in the IV's mode, but the divergence check is
the proof, not an argument.

Explicit non-consumers: `gimple-rvtt-invariant.cc`'s bounded
constant evaluator stays — SCEV is unavailable there by documented
phase-ordering (`AVOID_CFG_MODIFICATIONS` never establishes canonical
preheaders, `gimple-rvtt-invariant.cc:1944` rationale;
AUDIT-constants-residency.md notes). This is stated scope, not an
omission.

**COMPATIBILITY.** Two stages. **Stage A (CLASS-I):** the facade with
the legacy simulator as the deciding oracle and the classical analysis
as cross-check — corpus byte-identical x3, divergences surfaced as
build-time/dump facts only. **Stage B (CLASS-B):** flip the deciding
oracle to loop-iv/SCEV. Where the classical analysis proves *strictly
more* loops (shifted/decrementing/multi-use counters), new fires appear
under the *existing* replay/mop flags — this is an ON-delta on existing
knobs, so it takes the full ceremony including silicon on any row whose
delivery shape moves. If the widening delta is large, gate the widened
class behind `-mtt-tensix-optimize-symbolic-trips` `Init(0)` first and
promote by the established knob-promotion ceremony.

**GATES + PROOFS.** Twins: (a) fire twins for each newly-provable
counter shape (decrementing, shifted-step, multi-exit-refused);
(b) near-miss twin: a counter with a conditional step must refuse
(`niter-unproven`); (c) a wrap-around twin pinning modular behavior.
Existing refusals touched: every `*-trips-unproven`-class refusal in
replay hoist/peel/unroll and mop-form admission. New names:
`trip-oracle-divergence`, `niter-unproven`.

**EFFORT + RISK.** S–M (audit's class). Top failure mode: a symbolic
proof admitting a trip count the simulator would have refused, composed
with replay-buffer persistence (O1–O4) — i.e. wrong trips = wrong launch
arithmetic = silicon wedge. Mitigation: stage-A divergence census must
be EMPTY before stage B; carried-access rows (`reform` machinery) keep
their independent structural re-verification
(`reform_carried_launch_arithmetic_ok`), which is not weakened by this
item. Rollback: the facade keeps the legacy simulator compiled-in for
one extra pin; flipping the deciding oracle back is a one-line change.

**DEPENDENCIES.** None hard. Soft: #1 (register the new names).

**ACCEPTANCE.** `provable_constant_trips` and the mop-form duplicate are
deleted; the divergence census over the corpus is empty; at least one
previously-refused counter shape fires with a booked or
adjudicated-neutral result; widest single C→A conversion per the audit
(replay §1.4 grade retired).

---

### Item #3 — Generate hand-transcribed tables from their sources of truth

**DESIGN.** Three generators, one pattern (the tree already builds
`rvtt-combine.inc` from `rvtt.gc` via `build/genrvtt-combine` —
`tt/t-riscv-tt`; and already ships `.def` capability tables,
`rvtt-macro-tables-{wh,bh}.def`):

1. **Format-pair verdict table** (AUDIT-licensed-folds.md impr.3). The
   if-else ladders at `gimple-rvtt-store-fold.cc:577-605` (S2 sink) and
   `:790-798` (`pair_ok`) become a
   `tt/rvtt-storefold-verdicts.def` of rows
   `(load_mod0, store_mod0, verdict, divergence_class, license_token)`.
   A build-time checker (small script or gen tool run from
   `tt/t-riscv-tt`) recomputes the SHA256 of each cited
   `tt/proofs/store-sink-roundtrip/RESULT.txt` (they already carry
   per-pair stream commitments) and fails the build if a RESULT hash no
   longer matches the .def row's recorded hash — "retire the rule if
   the RESULT stops being EQUAL" becomes a build fact, not a README
   promise (`tt/proofs/README.md` contract).
2. **Madpair discovery vocabulary** (AUDIT-constants-residency.md
   impr.3). Extend `genrvtt-combine.cc` to emit a
   `rvtt_combine_will_fuse_p (stmt)` predicate alongside
   `rvtt-combine.inc`, generated from the same `rvtt.gc` rules the
   combiner fires; `madpair_vocab_mul_p`
   (`gimple-rvtt-prgm-const.cc:916-988`) and the immediate-fold
   vulnerability test (`hoisted_madpair_load_p`, `:1186-1209`) call it.
   Drift between discovery and combine becomes impossible by
   construction; every future vocabulary widening is automatic.
3. **crf apply/verify single plan interpreter**
   (AUDIT-replay-formation.md §1.9 + impr.4-part). `crf_apply`
   (`rtl-rvtt-replay.cc:6570`) and the shadow-contract verifier's
   hand-maintained order mirror (`crf_shadow_contract_ok:5349` "mirrors
   crf_apply exactly") both consume one `crf_plan_order (plan)`
   iterator; the verifier simulates the order the interpreter yields,
   the applier applies it. The dual-maintenance hazard is deleted, not
   patched.

**COMPATIBILITY.** CLASS-I. All three are representation changes of
decisions already made; the generated tables must reproduce the current
ladders' verdicts exactly (assert-equal checking build for one cycle on
(1) and (2); for (3) the final-lockstep re-verification
`counted-row-final-lockstep-divergence` belt already catches any
divergence fail-closed).

**GATES + PROOFS.** Corpus 3300/3300 x3 byte-identical. The existing
store-fold twins and crf twins pass unmodified. New build-time proof:
RESULT-hash check red/green demonstrable (mutate a RESULT copy in a
scratch build, watch it fail). No new refusal names; touches
`store-fold-*` and `counted-row-*` families read-only.

**EFFORT + RISK.** S–M. Top failure mode: an incorrect transcription
*into* the .def (same class of error it prevents thereafter) —
mitigated by the assert-equal checking build. Rollback: the ladders stay
in-tree, `#if 0`'d, for one pin; revert is a table swap.

**DEPENDENCIES.** None. Independent of #1 (different plumbing).

**ACCEPTANCE.** Adding a newly-swept format pair is a one-row .def edit
plus a proofs directory; the build fails when a cited RESULT file
changes; `madpair_vocab_mul_p`'s hand mirror is deleted.

---

### Item #4 — Unblock the audited-effect attribute migration

**DESIGN.** (AUDIT-regalloc.md impr.1; AUDIT-interprocedural.md impr.1
carries the sibling table-unification, planned here as one item with two
deliverables.)

Deliverable A — **effect attributes**: `effect_overrides` is copied
verbatim between `rtl-rvtt-lp-alloc.cc:333-358` and
`rtl-rvtt-dst-ownership.cc`, with the in-source admission that "the
principled home is the attribute family; that migration is blocked on a
planner-oracle re-freeze" (`lp-alloc:322-325`). `lane_gated_consumers`
(`lp-alloc:1328-1425`) is a ~100-entry hand `insn_code` allowlist.
Mechanism:
1. Execute the **planner-oracle re-freeze** that blocks the migration:
   re-run the macro-planner oracle suite at pin-head, freeze the new
   oracle baselines, and record the re-freeze as its own reviewed
   commit (this is the named blocker, not incidental).
2. Add generated md attribute rows to the `rvtt-cost.md` attribute
   family: `xtt_cc_write` (already partially present in
   `xtt_effect_set`), `xtt_lane_gated`, `xtt_lane_local` — refusing
   defaults, per the family invariant ("every default is the REFUSING
   value", `rvtt-cost.md:53-58`).
3. **Equality-assertion phase**: for one pin cycle, both consumers
   query attributes AND the legacy tables, `gcc_checking_assert`ing
   equality per insn per query (checking builds only). Then delete both
   copied tables.

Deliverable B — **one recorded-fact word table**: collapse the four
parallel audited-word classifiers — `classify_word_lreg`
(`gimple-rvtt-crosscall.cc:425`), `classify_word_init` (`:3399`),
`rvtt_raw_cc_word_class` (`rvtt-raw-boundary.cc:207-342`), and the
`rvtt_mop_audited_word_p` mirror — into one table
`word → {LREG, CC, PRGM, ADDR_MOD, Dst/RWC} verdicts` with per-face
query accessors, in `rvtt-raw-boundary.{h,cc}` (whose header already
names the extractor unification as owed, `:100-103`). The
preserving-vs-kill doctrine (a proven class is ambient-PRESERVING only,
never a KILL, because a raw word can sit swallowed in a REPLAY record
window — `rvtt-raw-boundary.h:96-103`) is carried as a *table property*,
not a per-consumer convention. A new audited opcode becomes one row +
provenance key instead of 3–4 coordinated edits.

**COMPATIBILITY.** CLASS-I in effect (verdict-identical by the
assertion phase), but the prompt-level honest classification is:
the re-freeze step is a *reviewed baseline move* and therefore carries
adjudication discipline — if any oracle row changes at re-freeze, that
delta is adjudicated like an ON-delta before the migration proceeds.
No new flags; corpus must stay byte-identical x3.

**GATES + PROOFS.** The equality-assertion checking build across the
full corpus and testsuite is the proof. Existing refusals touched: all
consumers of the four classifiers (`crosscall-*`, macro-planner ambient
walk, prgm-const TU scan) — verdicts must not move. New names: none.

**EFFORT + RISK.** M. Top failure mode: the two "identical" legacy
tables have already drifted (the divergence hazard the audit flags) —
in which case the assertion phase *finds a live bug*; the plan treats
any inequality as a P1 adjudication, not a blocker to route around.
Rollback: keep legacy tables one pin behind `#if`, revert the accessor
swap.

**DEPENDENCIES.** None hard. #7 (regrename) and every regalloc-family
consumer are raised by it; schedule before #7.

**ACCEPTANCE.** `effect_overrides` exists in exactly zero .cc files
(attribute-generated only); `lane_gated_consumers` is an attribute
query; one word-fact table serves all four scan faces; corpus
byte-identical x3.

---

## 2. MISSING CLASSICAL TECHNIQUES (roadmap items #5–#9)

### Item #5 — Rau iterative modulo scheduling + modulo variable expansion over a DDG

**DESIGN.** (AUDIT-scheduling.md impr.2, §F, §H.)

Classical formulation: **Rau's Iterative Modulo Scheduling** (Rau,
MICRO-27 1994): compute MII = max(ResMII, RecMII); place operations at
modulo issue slots against a Modulo Reservation Table (MRT) of II
columns; on conflict, evict (unschedule) minimally per a budget and
retry; on exhaustion, increment II and restart. **Modulo Variable
Expansion** (Lam, PLDI 1988): where a value's lifetime exceeds II,
unroll the kernel kmin = ceil(maxlifetime/II) times and rename per copy
to eliminate the overlap. GCC's own analogue is SMS
(`modulo-sched.cc`); LLVM's is MachinePipeliner — this item builds the
Tensix-shaped version inside the existing pass, not a port of either.

Mapping onto Tensix constraints:

- **DDG**: nodes = issued words of a self-loop row region; edges from
  the existing three-kind dependence vocabulary (`ls_dependence`,
  `rtl-rvtt-schedule.cc:1222-1233`): RAW/WAW latency-weighted (audited
  `xtt_result_latency`, refusing default), WAR issue-order. Cross-
  iteration edges carry distance 1 (deeper distances refused initially:
  `ims-dependence-distance-unproven`).
- **RecMII**: exact minimum over elementary circuits — the DT/EI
  chain-bound oracle already proves RecMII on the trig row (conf pin-42
  entry: "sem RecMII>=88 == hand"); implement as iterative
  shortest-paths / minimum cycle mean over the DDG, cross-checked
  against that oracle where both exist.
- **ResMII + MRT**: the reservation half already exists as pure data
  functions — `rvtt-macro-sched-core.h` occupancy/write-port/drain
  checkers ("nothing is hardcoded here", `:20-24`). The MRT is II
  columns of that model. This is why #11 (one timing model) is a hard
  dependency: the MRT and the acceptance simulator must be the same
  engine, or this item mints timing mirror number six.
- **MVE renames**: rename sets drawn from the 8-LREG file with the
  CC-atom domain rules as *rename vetoes* — the legality vocabulary of
  `ls_cyclic_rename_collisions` (`:1675-1827`) is kept as the veto
  predicate but the rename *structure* (which copies, which registers)
  is derived from lifetimes, not hand-managed collision webs. Register
  bound: kmin copies' concurrent lifetimes must fit 8 minus loop-live
  invariants — priced through #10's pressure engine, refusing
  `mve-rename-exhausted` (this is R1's doorway; see §4).
- **Replay/window interaction**: IMS schedules the *pre-formation*
  stream; window formation stays downstream (reform-mode discipline,
  `rtl-rvtt-replay.cc:7690-7702` deferral theorem). TTREPLAY and other
  `window_barrier`-class words bound the region exactly as
  `ls_admissible_p` does today (`:1125-1221`).
- **Acceptance**: strict whole-row cyclic-II-decrease over every issued
  word — the laneIJ acceptance rule that is documented load-bearing
  (raw CP-greedy regressed 94→95; conf pin-42 entry). Anything the IMS
  cannot prove better is refused and the existing schedule kept
  byte-identically; transactional accept/restore reuses the ls_
  machinery (`ls_schedule_region_cyclic:1902-1965`).

What it subsumes (retirement list, staged): the 6-copy convergence
probe `ls_cyclic_ii` (`:1849-1894`) → RecMII/ResMII; cross-row pairing's
1,800 lines (`crp_*`, `:2187-3997` — shape-locked unroll-and-jam,
textual `copy_insn` duplication, collision-web renames, Rule-B seeds,
three sub-knobs) → MVE with kmin=2 as a special case. crp is NOT
deleted until the IMS reproduces or beats every crp-booked win on
silicon (tanh 2-datum, mulint32, roundingops rows); until then both
coexist, IMS behind its own flag, arbitrated by measured acceptance.

**COMPATIBILITY.** CLASS-B. New flag `-mtt-tensix-optimize-ims`
`Init(0)`; the crp retirement (flipping default vocabulary from crp to
IMS) is a *second* ceremony after parity is proven.

**GATES + PROOFS.** Twins: fire twin on a two-region self-loop row;
generality twin with reversed ring direction and disjoint Dst rows
(the raw-ladder9 pattern); near-miss: an unaudited-latency member
refuses `ims-unaudited-latency`; an MVE case exceeding the register
bound refuses `mve-rename-exhausted`; a budget-exhaustion twin
(`ims-budget-exhausted`). RecMII cross-check against the DT/EI oracle
on the trig row is a required artifact. Existing refusals touched: the
`crp_*` refusal families (must keep firing while crp remains), the
cyclic-region acceptance names. Wrong-code belts carried over: bit-exact
by construction (renames + placement only), final lockstep re-verify in
the crf style where renames commit.

**EFFORT + RISK.** L. The riskiest item in the plan. Top failure mode:
MVE renames composing with capture rotation / interlock fill /
downstream formation to produce a stream the O1–O4 belts refuse late —
i.e. large engineering spend for refusal-parity, no wins. Mitigation:
acceptance-by-strict-II-decrease means the pass can never book a
regression; scope wave-entry to the two certified-floor rows (trig,
addrsqrt) plus the crp-booked rows as the proving ground. Rollback:
flag off = byte-identical; crp untouched until the parity ceremony.

**DEPENDENCIES.** Hard: #11 (the MRT/simulator must be the shared
engine), #10 (MVE pressure pricing). Soft: #7 (rename legality shared
vocabulary), #1, #4 (typed-effect attributes for veto queries).

**ACCEPTANCE.** On at least one certified-floor row the IMS either
achieves II < the certified region-local floor on silicon (trig: II 93
→ 92 or better, per the conf pin-42 cert) or re-certifies the floor
with IMS in the shape census; AND crp's booked rows reach measured
parity-or-better under IMS, enabling the crp retirement ceremony.

---

### Item #6 — Briggs conservative coalescing in lp-alloc before spill selection

**DESIGN.** (AUDIT-regalloc.md impr.2.)

Classical formulation: **Briggs conservative coalescing** (Briggs,
Cooper, Torczon, TOPLAS 1994): merge copy-related nodes a and b iff the
merged node has fewer than k neighbors of significant degree (degree ≥
k), guaranteeing coalescing never turns a k-colorable graph
uncolorable. k = 8 (`SFPU_REG_NUM`). Optionally George's test for
precolored pairs (merge b into precolored a iff every neighbor of b
either interferes with a or has degree < k).

Mapping: `rtl-rvtt-lp-alloc.cc` already recognizes copies
(`move_src_node`, `:1202-1207`) but only to suppress the interference
edge. Insert a coalescing phase into `enforce_colorability`
(`:1814-1933`) between graph build and DSATUR: iterate conservative
merges to fixpoint on the existing n×n sbitmap graph (degree recompute
per merge; kernel-scale n makes O(n²) per round acceptable — same
budget philosophy as the existing 256-round bound). The colorability
verdict and spill-victim selection then run on the coalesced graph, so
a web that only spilled through a Dst round trip because its copy
halves were counted separately becomes 8-colorable for free.
Assignment stays delegated to IRA (the file's stated phase contract,
`:63-69`) — this item changes which webs *spill*, never who assigns.

The transactional discipline is untouched: coalescing is a graph-side
computation; only spill decisions differ, and every spill still runs
through `spill_transaction`/rollback and the `bind_commit_sound_p`-class
independent oracles.

**COMPATIBILITY.** CLASS-B — fewer spills = different generated code on
pressure-9+ functions. New flag `-mtt-tensix-optimize-lreg-coalesce`
`Init(0)` riding inside the existing lreg-alloc gate (one knob = one
mechanism).

**GATES + PROOFS.** Twins: a fire twin where a copy-related web spills
uncoalesced and colors coalesced (assert the Dst round-trip words
disappear); a conservative-test near-miss (merged degree ≥ 8 refuses
the merge, spill unchanged — dump line
`coalesce-conservative-degree`); a precolored-pair twin if George's
test ships. Existing refusals touched: `lreg-pressure-exceeded`
(spill-diag) should fire strictly less often — census the delta.
The IP-2 successor is in scope here: erfinv's dst-ownership fold
refusing `lreg-pressure-exceeded` (9 > 8) at ON-36 (conf pin-48 entry)
is the seeded witness row — coalescing is one of the two mechanisms
(with #13's pricing) named to relieve it.

**EFFORT + RISK.** S (audit: ~100 generic lines). Top failure mode: a
merge that is conservative for colorability but extends a lifetime
across a CC/Dst-epoch boundary the spill-legality lattice would have
handled differently — mitigated because coalescing here only *removes*
spills; the lpa_state legality analysis runs unchanged on whatever
spills remain. Rollback: flag off.

**DEPENDENCIES.** None. Independent of #4 (uses graph structure, not
effect tables).

**ACCEPTANCE.** erfinv (the IP-2 witness) either folds without
`lreg-pressure-exceeded` at ON-36+flag or the refusal is re-certified
with coalescing in the census; corpus spill-round-trip word count
strictly decreases on the pressure-suite rows with zero new losses.

---

### Item #7 — Du-chain regrename over post-RA Tensix regions

**DESIGN.** (AUDIT-regalloc.md impr.4; the pass itself names the
formulation: "This is regrename's classic du-chain problem
(gcc/regrename.cc) scoped to the capturable-row shape",
`rtl-rvtt-lreg-rename.cc:29-31`.)

Classical formulation: **post-RA register renaming over def-use
chains** (`gcc/regrename.cc`): build du-chains per hard register from
operand/constraint analysis across the region; for each chain, find the
best alternative register whose lifetime is free across the chain span;
rename the whole chain. Purpose: break false (output/anti) dependencies
that a coloring created by register reuse.

Mapping onto Tensix:

- **Chain construction**: reuse `regrename.cc`'s chain infrastructure
  (`regrename_analyze` on the region, or a scoped reimplementation over
  the same du-chain shape if the harness's region granularity doesn't
  fit `regrename.cc`'s bb_info assumptions) restricted to the LREG
  class, over post-RA Tensix regions — generalizing the current
  single-shape engine (self-loop blocks, single-SET latency-0
  invariant-input members only, `rtl-rvtt-lreg-rename.cc:97-159,
  230-237`).
- **Typed-effect veto** (the Tensix half): a chain is rename-admissible
  only if every member's effects are audited and none of: CC write
  without word-exact all-lanes proof, config-dest write, RWC/Dst
  counter effect, replay owner/boundary class, LUT implicit-slot
  register, companion-bank coupled pair (`span_companion_sound_p`
  vocabulary), raw `.ttinsn`. Veto queries go through
  `rvtt_insn_effects` — which is why #4 (attribute migration) precedes:
  the veto must read one table, not the copied ones.
- **Target selection**: free LREGs from DF live ranges across the chain
  span; deterministic order (lowest free index) for stability.
- **Decouple payoff from admission** (audit's explicit instruction):
  delete the `row_has_audited_stall_p` gate (`:165-173`). The renamer
  renames whenever a chain is legal and a *consumer* requests it; the
  consumers price. Consumers: interlock fill (kills the
  register-serialized filler case — the trig L5 serialization, conf
  pin-42), capture rotation, crp/IMS (#5) rename supply, and R1.
  Mechanically: the pass exports
  `rvtt_lreg_rename_chain (region, chain, target)` as a service with
  its own legality proof, plus a standalone pass mode that renames
  storage-collision chains greedily under a whole-row
  no-worse acceptance (same strict-acceptance discipline as laneIJ's
  cyclic acceptance).

**COMPATIBILITY.** CLASS-B. New flag
`-mtt-tensix-optimize-lreg-rename-chains` `Init(0)`; the existing
single-shape pass keeps its flag and behavior until the general pass
subsumes its fires (parity census, then retirement ceremony).

**GATES + PROOFS.** Twins: fire twin = two-chain rename in a self-loop
row the old pass refuses (multi-member class); generality twin renamed/
reordered; near-miss twins per veto class (CC-writer chain refuses
`regrename-effect-veto`, no-free-LREG refuses
`regrename-no-free-lreg`, open chain at region exit refuses
`regrename-chain-open`). Bit-exactness by construction (renames only);
the shape census (delivered words unchanged, only register fields move)
is asserted in the twins. Existing refusals touched: the old pass's
named refusals stay until retirement; interlock fill's
"latency beyond audited window" behavior must be unchanged when flag
off.

**EFFORT + RISK.** M–L (audit's class). Top failure mode: a rename that
is legal per typed effects but changes a *positional* hazard discharge
(delay-shadow/lockstep classes, O4) — mitigated by running the crf
shadow-contract re-verification over any region where a rename commits,
and by the veto refusing `XTT_DELAY`-class members outright in v1.
Rollback: flag off; service consumers must treat rename-unavailable as
a normal refusal.

**DEPENDENCIES.** Hard: #4 (one effect table), #10 (a rename changes
the pressure peak; rename-aware pressure = shared engine — the
COMPILER-AUDIT-REPORT.md §(d) states #10 is the precondition, not an
accessory). Soft: #1.

**ACCEPTANCE.** (Verbatim the charter's example, adopted:) trig is
re-attacked at the certified floor; either the +0.63 flips or the floor
is re-certified with the rename in the shape census. Additionally the
single-shape pass's fires are a strict subset of the general pass's
fires on the corpus.

---

### Item #8 — Multi-partial-accumulator splitting for loop-carried reassoc chains

**DESIGN.** (AUDIT-licensed-folds.md impr.5-tail; the standing refusal
is `reassoc-loop-carried-underived`, recognition scaffolding
`note_loop_carried`, `gimple-rvtt-reassoc.cc:92-101, 698-764`.)

Classical formulation: **reduction variable expansion / interleaved
reduction** — upstream does it in the vectorizer's reduction handling
(`tree-vect-loop.cc` reduction chains) and in the RTL unroller's
`-fvariable-expansion-in-unroller`: a loop-carried associative chain
`acc = acc ⊕ x_i` with serial RecMII = latency(⊕) is split into P
partial accumulators `acc_j = acc_j ⊕ x_{i:i%P==j}` reduced after the
loop, cutting the recurrence bound by P at the cost of P−1 extra live
registers and a post-loop reduction tree.

Mapping: the recognition walk already exists (loop-carried same-op
chain via PHI cycle, currently diagnostic-only with an 8-step bound).
Transform: (a) choose P = min(ceil(latency-derived benefit), pressure
headroom from #10, 4); (b) create P−1 new accumulator PHIs initialized
to the operation identity (0.0 for add — identity legality is part of
the license argument: −0.0 + x vs x is exactly the class of
bit-divergence the FP license ratifies; the proof note states it);
(c) rewrite the chain round-robin; (d) emit the post-loop balanced
reduction via the existing `rebalance_build` machinery. FP legality
rides the existing two-key licensing wall
(`rvtt_reassoc_fp_licensed_p`: `-fassociative-math` + target token) —
the audit's reading is the license "already covers it in principle";
this plan still ships it as its own token
`-mtt-tensix-optimize-reassoc-loop-carried` `Init(0)` for one-knob-one-
mechanism attribution, with the licensed-refusal invariants enforced by
#1's helper. Integer chains need no FP license (exact associativity,
same split as today's integer rebalance).

**COMPATIBILITY.** CLASS-B, licensed. Token absent ⇒ the standing
`reassoc-loop-carried-underived` refusal continues byte-identically.

**GATES + PROOFS.** Twins: fire twin (serial FP accumulation loop,
P=2, assert the recurrence split in the dump + bit-golden under the
license's declared divergence class); pressure near-miss (headroom 0 ⇒
refuse `reassoc-partials-pressure`); depth near-miss (chain shorter
than benefit threshold ⇒ `reassoc-partials-unprofitable`); the 8-step
recognition bound retired or renamed with a widened proof. Proof
obligation: a divergence statement in the licensed-fold style — the
value-change class is re-association order only, quantified on the twin
inputs; no new tt/proofs sweep is required (the class is already
ratified for reassoc), but the token text cites the reassoc license
lineage. Board targets: reduction-shaped rows in the loss/parity tail
and the trig "value-changing circuit restructure" successor class named
in the pin-42 cert (muli+add → fused mad lineage — same license
family).

**EFFORT + RISK.** M. Top failure mode: pressure mispricing — P
partials starving the loop body and flipping a win row (the
digamma-fresh starvation precedent, `gimple-rvtt-prgm-const.cc:
3966-3974`); mitigated by hard dependency on #10's residual-capacity
query and by the strict per-row acceptance. Rollback: token off.

**DEPENDENCIES.** Hard: #10. Soft: #1, #2 (trip counts for post-loop
reduction placement in counted loops).

**ACCEPTANCE.** `reassoc-loop-carried-underived` no longer appears on
any corpus row whose chain fits the licensed class and the pressure
budget; at least one silicon row books a win or certifies a floor with
the split in the census.

---

### Item #9 — Suffix-automaton discovery for replay formation

**DESIGN.** (AUDIT-replay-formation.md impr.5-part; the self-admitted
O(N²) at `rtl-rvtt-replay.cc:448`, the knapsack note at `:7523`, the
"first N insns" prefix loss at `:63-65`.)

Classical formulation: **maximal-repeat enumeration via a suffix
automaton** (equivalently suffix tree — the MachineOutliner's
structure): map each insn to a stable integer symbol; build the suffix
automaton of the symbol sequence in O(N); every state represents a
right-extension-maximal set of repeated factors with its occurrence
count (endpos size) computable by one pass over the suffix links;
enumerate candidates = states with occ ≥ 2 and length ≥ MIN_SEQUENCE,
left-extension-maximality checked per state.

Mapping:

- **Symbol mapping**: reuse the existing hasher exactly — the crc32
  class hash *including the register generation counter* (`reg_ages`,
  `:236`), which is the piece that makes textual equality value-safe;
  two insns get one symbol iff the current `rtx_equal_p` re-check
  (`:407`) would pass. Illegal/boundary insns (barriers, owner words,
  clobber classes — everything `scan_insns` refuses today) become
  unique sentinels that break the sequence into legal segments; the
  automaton is built per segment concatenation with separators, so no
  candidate ever crosses a boundary — admission semantics are
  *unchanged by construction*.
- **Candidate layer**: maximal repeats replace grow-by-one discovery;
  prefix candidates ("first N insns" of a longer repeat) are subsumed
  because every prefix of a maximal repeat is enumerable from the
  automaton where profitable — feed the same `pick_replay` scoring and
  interval-subtraction allocator unchanged in stage A.
- **Selection** stays greedy pick + invalidate in this item; upgrading
  selection to the BnB (the in-tree exact solver) over the candidate
  set is an optional extension once #12 gives one pricing function —
  noted, not planned (see §8).

**COMPATIBILITY.** Two stages. **Stage A (CLASS-I):** the automaton
enumerates; a checking build asserts its candidate set is a superset of
the legacy discovery's set and that the *selected* windows are
identical (tie-breaks pinned to the legacy order). Corpus byte-identical
x3. **Stage B (CLASS-B):** admit the additional candidates (longer
maximal repeats, non-anchored prefixes) — new formations under the
existing replay flags = ON-delta ceremony, or behind
`-mtt-tensix-optimize-replay-maximal-repeats` `Init(0)` if the delta is
wide.

**GATES + PROOFS.** Stage-A superset assertion over the corpus is the
proof artifact. Stage-B twins: a fire twin where the legacy discovery's
prefix bias loses a longer interior repeat; near-miss: a repeat
crossing a barrier sentinel never becomes a candidate (assert by
construction + twin). Existing refusals untouched (`replay-raw-capture-
present`, MIN_SEQUENCE floor, slot budget all downstream of discovery).

**EFFORT + RISK.** M. Top failure mode: subtle mismatch between symbol
equality and `rtx_equal_p` (hash collisions are already handled by the
re-check; the risk is the *reverse* — symbols distinguishing what the
legacy path merged, silently shrinking candidates) — the stage-A
superset assertion is the catch. Rollback: discovery is a swappable
front-end; keep `scan_insns` compiled for one pin.

**DEPENDENCIES.** None hard. Soft: #1; composes with #7/R1 (discovery
over a renamed collision-free stream finds more repeats — the audit's
stated R1 linkage) and #12 (one pricing function for stage-B deltas).

**ACCEPTANCE.** The O(N²) FIXME comment and the first-N-insns
limitation are deleted with the code that carried them; stage-A corpus
identity holds; stage-B books at least one adjudicated formation the
legacy discovery provably could not enumerate.

---

## 3. INFRASTRUCTURE UNIFICATION (roadmap items #10–#15)

### Item #10 — One pressure/liveness engine

**DESIGN.** (AUDIT-licensed-folds.md impr.2, AUDIT-constants-
residency.md impr.2, AUDIT-interprocedural.md impr.3.)

New module `tt/rvtt-pressure.{h,cc}` — GIMPLE-side vector-register
pressure, computed once per function, queried incrementally:

- **Core**: the best of the three existing mirrors is the seed —
  `compute_lreg_pressure` (`gimple-rvtt-prgm-const.cc:1408-1545`), a
  backward may-live fixpoint on GCC bitmaps over `SSA_NAME_VERSION`
  with per-point peaks and fail-closed width handling (`lreg_width`
  returns whole-file for unknown modes). Promote it to the module;
  gimple uids set once (`renumber_gimple_stmt_uids`) so all point
  comparisons are O(1).
- **Retired mirrors**: `rvtt_reassoc_bb_vec_pressure_peak`
  (`gimple-rvtt-reassoc.cc:140-221` — O(names×stmts) per candidate,
  also called per mad-fuse candidate from `rvtt.gc:240`);
  `rvtt_loop_lreg_pressure_legal_p` (`gimple-rvtt-invariant.cc:347-615`
  on `std::unordered_*`); the cc-transient charges (`invariant.cc:
  588-602`) become declared per-insn facts consumed from the effect
  table (#4).
- **API**: `pressure_map (fn)` built once; `peak (region)`;
  `peak_with_delta (region, candidate_delta)` — the incremental
  residual-capacity query so greedy selectors (crossloop's, invariant's
  `select_pressure_legal_loads:739-766`, park's budget) stop re-running
  full proofs per candidate; `residual (region)`.
- **Parameters as data**: `SFPU_REG_NUM` = 8 read in exactly one place;
  creg exemption derived from the insn table as an operand-class fact
  (replacing the hardcoded LUT slot-id switch,
  `invariant.cc:395-406` — the laneHF class of bug becomes
  unrepresentable); width table shared.
- **Conservatism contract**: the engine must be *at least as
  conservative* as every retired mirror at every query point, and in
  stage A *exactly equal* — the mirrors' comments already state they
  "deliberately mirror the conservative counting"
  (`prgm-const.cc:1411-1413`), so equality is the designed state.

**COMPATIBILITY.** CLASS-I. Stage A: engine computes alongside all
three mirrors, `gcc_checking_assert` equality at every query (checking
build, full corpus + testsuite); stage B (same pin or next): delete
mirrors. Corpus byte-identical x3 throughout. Any inequality found is a
live latent divergence and is adjudicated as a finding, not patched
around.

**GATES + PROOFS.** The assertion phase is the proof. Existing refusals
touched read-only: `reassoc-pressure-budget-exceeded`,
`crossloop-pressure`, `crosscall-callee-pressure`,
`lreg-file-exhausted` — fire sets must be identical pre/post. New
names: none (CLASS-I).

**EFFORT + RISK.** M–L. Top failure mode: an "equal-but-faster" query
that is equal on the corpus and unequal on some testsuite shape —
that's what the testsuite assertion run catches; second failure mode is
compile-time regression from building the map for functions that never
query (mitigate: lazy build on first query, the existing lazy-budget
idiom, `prgm-const.cc:4369-4374`). Rollback: mirrors stay in-tree
`#if`'d one pin.

**DEPENDENCIES.** None. It is the precondition of #5, #7, #8, #13, R1.
Schedule earliest of the L-class items.

**ACCEPTANCE.** Three mirrors deleted; **8** appears as a capacity
constant in exactly one translation unit; per-candidate re-proof loops
replaced by residual queries (assert: invariant-loadi's selector no
longer calls a full-function walk per candidate); corpus byte-identical
x3.

---

### Item #11 — One timing model instead of five

**DESIGN.** (AUDIT-scheduling.md impr.1.)

New module `tt/rvtt-timing.{h,cc}` — one reservation/latency engine,
IR-free in the `rvtt-macro-sched-core.h` style (pure functions over
plain data; the audit names that core as the template):

- **Facts in, once**: audited `xtt_result_latency` (+1-biased encoding
  preserved; 0 = unaudited = refuse), `xtt_next_slot_stall`,
  `xtt_delay`, issue classes, drain slots — all read through
  `rvtt_insn_effects`, never re-derived.
- **Engine API**: `simulate (word_seq) → {makespan, per-slot stalls}`
  (replaces `ls_simulate`, `:1240-1273`, and the prera makespan mirror,
  `rtl-rvtt-lp-schedule-prera.cc:96-105`); `adjacent_stall (a, b)`
  (replaces `adjacency_stall`/`audited_latency`, `:712-751`, keeping
  the refuse-beyond-audited-window discipline);
  `cyclic_ii (word_seq)` — initially the 6-copy convergence probe moved
  verbatim (CLASS-I), later the RecMII/ResMII exact form as #5's MRT
  lands (one engine, two precision tiers, callers choose);
  `delivery_slots (...)` hooks for the ds_* pricing mirrors
  (`rvtt-schedule.h` downstream-mirror constants,
  `rtl-rvtt-replay.cc:1535`) — jointly owned with #12: #12 owns the
  words→centislot economics, #11 owns the execution-side stall/latency
  simulation both consume.
- **Consumers converted** (each a separate verdict-identical sweep):
  interlock/latency fill, ls list scheduler, ls cyclic, prera, replay
  pricing's `exec_interlocked_slots` (`rtl-rvtt-replay.cc:1596` — its
  16-register ready[] scoreboard becomes the engine's), macro-sched
  core (becomes the engine's inner layer rather than a parallel one).
- **Optional later stage** (data-blocked, see §8): express the audited
  facts as real per-insn DFA reservations + `define_bypass` when tuned
  per-CPU latencies land (the F1.3 IOU, `rvtt-cost.md:11`).

Drift between models becomes structurally impossible — the lane-BM
SFPSWAP convention comment class (`:718-727`) retires.

**COMPATIBILITY.** CLASS-I. Every consumer conversion is
verdict-identical: the engine must reproduce each replaced simulator's
outputs exactly on the corpus (per-consumer checking-build assertion,
one consumer per commit). Corpus byte-identical x3.

**GATES + PROOFS.** Per-consumer equality assertions; the pressure
suite and the frozen-16 discipline. Existing refusals touched
read-only: `pressure-oracle-disagreement` (prera's belt must keep
firing on the same synthetic twins), the fill's audited-window
refusals. New names: none.

**EFFORT + RISK.** L. Top failure mode: two simulators that "must agree
by convention" today *don't* on some shape — equality assertion finds
it; the adjudication rule is: the engine adopts the verdict of the
consumer being replaced (bug-compatible per consumer) and files the
divergence as a finding, so CLASS-I is preserved even over latent
drift. Rollback: per-consumer revert (each sweep independent).

**DEPENDENCIES.** None hard. #5 hard-depends on it. Do not schedule #5
into a wave before this lands.

**ACCEPTANCE.** `adjacency_stall`, `ls_simulate`, `ls_cyclic_ii`, the
prera makespan mirror, and the replay ds_* execution mirrors are
deleted; one engine serves fill/list/cyclic/prera/pricing; corpus
byte-identical x3.

---

### Item #12 — One delivery-cost API

**DESIGN.** (AUDIT-cost-model-refusals.md impr.3, AUDIT-replay-
formation.md impr.2, AUDIT-addressing-delivery.md impr.1.)

New module `tt/rvtt-delivery-cost.{h,cc}`:

- **Constants in, once**: the `XTT_REPLAY_COST_*_X100` centislot
  constants and budget constants stay in `rvtt-cost.md`
  `define_constants` (the GCC-idiomatic carrier, compiled into
  `insn-constants.h`) — the module is the *only* consumer that turns
  them into arithmetic.
- **API**:
  - `words_to_centislots (n, plane)` — RISC-push vs replay-slot planes;
  - `config_cost (loadi_forms)` — subsumes `config_word_loadi_issues`
    (`rtl-rvtt-macro-planner.cc:263-277`);
  - `run_amortization {entry_cost, per_run, body, trips/weights} →
    verdict` — the cross-multiplied integer inequality
    (`config*entry + per_run*body < explicit*body`), one spelling
    (retires the 48-bit scaling loop duplicated between
    `crosscall.cc:4349-4353` and macro-planner `loop_trip_weight`);
  - `replay_pricing {trips, words, exec_ilk, launch_run, shape,
    drain_contract} → {benefit_x100, refusal}` — one constructor-
    argument for the drain-inclusive contract instead of a branch
    forest; replaces the three spellings in `hoist_profitable_p`
    (`rtl-rvtt-replay.cc:1688`), `counted_peel_profitable_p` (`:3205`),
    and the window-sizing delivered-issue count (`:1017-1040`);
  - `clones_word_exact_p (...)` — ONE word-exact lockstep comparator
    replacing the three copies (`:407`, `:712`, `:876`);
  - frontend-cover queries — one spelling of the per-item/per-raw-insn
    issue-word cover (`rtl-rvtt-dst-autoincr.cc:1022-1030` vs
    `:2533-2537`, equivalence currently asserted by comment), and the
    `autoincr_caps` positional literals re-expressed as named struct
    initializers cross-checked against `rvtt-cost.md` constants at
    build time.
- **Closes the two named MODEL SEAMS** in delivery-shape's own header
  (`gimple-rvtt-delivery-shape.cc:64-78`): the dst-autoincr W_drain
  term and the record-hoist mirror join the same module, so the
  delivery-shape argmin, the macro-planner WP13 arbitration
  (`:354-475`), and prgm-const's peel break-even
  (`gimple-rvtt-prgm-const.cc:3688-3691`) all price through one door.

**COMPATIBILITY.** CLASS-I. Fixed-point arithmetic re-hosted must be
bit-equal (integer arithmetic, so equality is exact); per-consumer
assertion sweeps as in #11. Corpus byte-identical x3.

**GATES + PROOFS.** Per-consumer verdict-equality assertions; the
refusal-biased one-sidedness of the WP13 arbitration (alternative
priced at steady-state lower bound) is preserved and documented as a
module invariant with a unit test (the module is IR-free, so it gets a
standalone test like `rvtt-macro-sched-test.cc`). New names: none.

**EFFORT + RISK.** M. Top failure mode: a silent semantic difference
between "equivalent" cost spellings (exactly the drift the audits flag
as maintained-by-comment) — adjudicated as findings under the
bug-compatible rule (#11's). Rollback: per-consumer revert.

**DEPENDENCIES.** None hard. #13 and R1 pricing depend on it; #2
deletes one of its duplicate constant-trip consumers first (either
order works; the facade wins if #2 lands first).

**EFFORT NOTE.** The mop-form trip-prover duplication is #2's; this
item takes the cost spellings only.

**ACCEPTANCE.** PUSH/SLOT arithmetic exists in one translation unit;
delivery-shape's MODEL SEAMS comment is deleted with the seams; the
three replay pricing spellings and three lockstep comparators are one
each; corpus byte-identical x3.

---

### Item #13 — One placement arbiter for invariant constants

**DESIGN.** (AUDIT-constants-residency.md impr.1, AUDIT-regalloc.md
impr.3, AUDIT-interprocedural.md impr.5 — and the code's own named
successor: "a finer per-authority priced arbitration",
`gimple-rvtt-invariant.cc:1770-1771`.)

Today one decision — where a loop-invariant constant lives — is
co-owned by: invariant-loadi's early hoist + park-ordering deferral
heuristics (`in_region >= 3` demand gate, LUT-body wholesale defer
keyed to one measured decay 29861→43447, six pin-34 loss rows encoded
as structural predicates — `invariant.cc:1636-1822`), lut-select's
coefficient placement (`gimple-rvtt-lut-select.cc:1380-1420`), and the
residency walk's four classes + tiers + store-source-tier fallthrough
under 7+ interacting flags (`riscv.opt:645-683`), with final selection
an O(n²) insertion sort on a uses-then-value key whose suboptimality is
self-named (`prgm-const.cc:3966-3987`).

Mechanism — a single late-GIMPLE arbitration:

- **Candidate model**: per invariant constant, enumerate the full
  alternative set: {stay-in-loop, preheader LREG park, PRGM park
  (L12–L14), remat-at-use, LUT slot word, peel-placed (pre-peel head)}.
  Each authority (invariant hoist, residency walk tiers, lut-select
  coefficients, store-source tier) stops *deciding* and starts
  *bidding*: it contributes legality proofs (unchanged — CC-canonical
  peel proofs, ambient proofs, consumer audits, slot-clobber censuses
  all stay exactly as they are) and a priced bid.
- **Pricing**: delivery words via #12 (materialization cost,
  `rvtt_sfpxloadi_materialization_cost` lineage; peel delivery-class
  economics), pressure delta via #10 (`peak_with_delta`), trip
  amortization via #12's `run_amortization`. The digamma-fresh
  starvation that vetoed a words-saved key (`prgm-const.cc:3966-3974`)
  is resolved the way the audit diagnoses: it was pricing without a
  pressure term — the arbiter prices both.
- **Selection**: greedy by priced net benefit under residual capacity
  (deterministic tie-breaks), with the in-tree BnB available for small
  contended sets (same 24/32 caps; the solver is IR-free and idle
  here otherwise).
- **Retirement list** (stage B): the `in_region >= 3` cut, the
  LUT-body wholesale defer, the uses-then-value rank, the
  store-source-tier ordering branch, the park-ordering/lut-authority/
  madpair handshakes distributed across three passes. The flags
  collapse: existing residency flags remain as legality gates; the
  *ordering* knobs deprecate.

**COMPATIBILITY.** Staged, per the charter's classification of #13 as
infra: **Stage A (CLASS-I)**: the arbiter is built and run in *shadow
mode* — it computes priced decisions and asserts/dumps agreement or
disagreement with the current policy chain, changing nothing. The
disagreement census over the corpus is the design's proof artifact
(expected: small, concentrated on the known mis-arbitration rows).
**Stage B (CLASS-B)**: `-mtt-tensix-optimize-priced-placement`
`Init(0)` switches decisions to the arbiter; full ceremony; the
measured-anatomy cut lines retire only after the board re-measure.

**GATES + PROOFS.** Stage-B twins: per-alternative fire twins (a
constant that flips park→remat under pressure; a LUT coefficient that
takes preheader materialization; a peel-class pre-peel placement — the
laneIN shape); near-miss: unpriceable alternative refuses
`place-alternative-unpriceable`; budget exhaustion refuses
`place-budget-exhausted`. Board proof obligation (stage B): the six
pin-34 loss rows (ceil/rops/rdiv/sqrt/softsign/i0), softplus,
hardsigmoid-fresh, gelu, digamma-fresh — every row whose history is
encoded in the current cut lines — re-measured; no regression
unaccounted. Existing refusals touched: `residency-walk-ordering`,
`park-prepeel-ambient-unproven` (legality — unchanged),
`lreg-file-exhausted`.

**EFFORT + RISK.** L. Top failure mode: the priced model is *worse*
than the measured anatomy on some row the anatomy was fit to (that is
what the cut lines are: local optima protected by measurement) — the
shadow-mode census makes this visible before any behavior moves, and
stage B is refusable row-by-row because acceptance is silicon-gated.
Rollback: stage-B flag off restores the legacy policy chain verbatim
(the chain is not deleted until the retirement ceremony after the
board re-measure).

**DEPENDENCIES.** Hard: #10, #12. Soft: #14 (peel/cc-lift legality
queries), #2 (trip facts).

**ACCEPTANCE.** Softplus's hand-arm residual (+0.09, "last word = walk
candidate-ranking polish", conf pin-44 entry) flips or is re-certified
with the arbiter's priced decision in the census; the `in_region >= 3`
and LUT-decay cut lines are deleted from `invariant.cc`; no board row
regresses.

---

### Item #14 — CC-region tree

**DESIGN.** (AUDIT-licensed-folds.md impr.4; the audit calls it the
biggest single generality unlock.)

New analysis `tt/rvtt-cc-region.{h,cc}` — GIMPLE-side, computed once
per function:

- **Structure**: a region tree of pushc/popc frames: node = {parent,
  depth, entry stmt, exit stmts, mask-refinement facts (which x-form
  refinements narrowed the frame), SFPENCC-poisoned flag (SFPENCC can
  enable beyond the enclosing mask — poison propagates upward),
  all-lanes facts (word-exact SFPENCC identity via
  `rvtt_all_lanes_encc_p`)}. Built by one linear walk per BB plus CFG
  stitching over the structured lowering's canonical forms; anything
  unstructured (unbalanced depth, indirect flow through a frame,
  depth > architectural 8) marks the enclosing subtree
  `cc-region-unstructured` — queries against it refuse, fail-closed.
- **Queries**: `region_of (stmt)`, `same_frame_p (a, b)`,
  `parent_frame_p (a, b)`, `region_all_lanes_p (r)`,
  `refinement_chain (r)`, `poisoned_p (r)`.
- **Consumers re-expressed** (each its own conversion): store-fold's
  `classify_assign_to_store` v_endif diamond (`gimple-rvtt-store-fold.
  cc:383-463`) becomes "store's frame == assign's parent frame";
  `check_load_to_assign` (`:469-501`) becomes a refinement-chain query;
  ccmask's `WANT_XVIF→…→WANT_POPC` state machine
  (`gimple-rvtt-ccmask.cc:213-438`) keys admission off the region tree;
  reassoc's window walk CC-arm; lut-select's `match_group` region
  discipline (partial — the coefficient/select-tree matching stays).
  The scope is GIMPLE; RTL-side ambient walks (macro-planner) are #4/
  #15 territory.

**COMPATIBILITY.** Staged. **Stage A (CLASS-I)**: tree built, queries
implemented, consumers converted under an equivalence constraint — each
converted recognizer must accept exactly its old shape set (the tree
query is *restricted* to the old shape via a compatibility predicate);
assertion phase compares old and new admission verdicts corpus-wide.
**Stage B (CLASS-B)**: drop the compatibility restriction behind
`-mtt-tensix-optimize-cc-region-general` `Init(0)` — semantically
identical layouts the shape matchers refuse today (different block
placement of the same frame structure) begin to fire. Full ceremony;
this is the item's payoff and R2's foundation.

**GATES + PROOFS.** Stage-B twins: fire twin = a v_endif-equivalent
layout with reordered blocks that the diamond matcher refuses today
(the near-miss twin of the current suite becomes the fire twin — its
FUTURE-VERDICT flips, which is exactly what FUTURE-VERDICT oracles are
for); near-miss = an SFPENCC-poisoned frame keeps refusing
(`cc-region-poisoned`); unbalanced-depth twin
(`cc-region-unstructured`). Proof obligations: the S2 sink's
*denotational* half (format round trip) is untouched — only the shape
obligation generalizes; every licensed fold keeps its proof scope.
Existing refusals touched: `store-fold-*` shape refusals, ccmask's
candidate gates, `ccmask-zero-shared` (unchanged), reassoc window
barriers.

**EFFORT + RISK.** M–L. Top failure mode: the tree admitting a frame
whose *dynamic* mask differs from the shape-matched guarantee (e.g. a
refinement the x-form vocabulary doesn't cover) — mitigated by the
fail-closed unstructured default and by keeping the refinement
vocabulary a positive allowlist (same words the matchers trust today).
Rollback: stage-B flag off; stage-A conversions are shape-equivalent.

**DEPENDENCIES.** None hard. R2 hard-depends on it. Soft: #1.

**ACCEPTANCE.** One analysis answers frame questions for ≥4 consumer
passes; a reordered-layout twin that refuses at pin-48 compiles to the
folded form under the stage-B flag with bit-golden output; ccmask's
state machine is a query client.

---

### Item #15 — IPA summaries for the whole-body rescans

**DESIGN.** (AUDIT-interprocedural.md impr.4, AUDIT-addressing-
delivery.md impr.3, AUDIT-constants-residency.md impr.5.)

Three conversions to pass-manager-owned summaries
(`function_summary<T>` with cgraph hooks — the `ipa-ra` shape):

1. **Crosscall contracts**: per-function summary {registers provably
   inert/clobbered, delivered-word facts, init/addrmod face facts}
   computed once per body, consulted per call edge — replacing the
   per-callee whole-caller-body rescans (`scan_stmt`,
   `gimple-rvtt-crosscall.cc:2086-2234`) and unifying the duplicated
   init/addrmod caller-chain walks (`:4140-4192` vs `:4483+`). The
   multi-call-site refusal `crosscall-caller-multi-site` falls out
   naturally (a summary is site-count-independent) — but *widening*
   admission to multi-site callers is stage B, not a refactor
   side effect.
2. **Mop-form outward ownership**: the cover-state must-dataflow over
   the transitive caller closure (`rtl-rvtt-mop-form.cc:1092-1980`)
   becomes a GIMPLE-time summary consumed by the RTL pass, replacing
   the RTL-reads-caller-GIMPLE inversion and its expansion-ordering
   assumption. In the same move, **retire the asm-mnemonic parser**
   (`mop_classify_asm`, `:1404-1502`): tighten caller-event
   classification to typed builtins + the canonical single-constant
   `.ttinsn %0` form only. The audit states this loses nothing on the
   mapped corpus; the corpus census verifies — any TU that changes
   verdict moves that tightening into a stage-B adjudication instead.
3. **prgm-const `tu_facts`**: the TU-lifetime static
   (`gimple-rvtt-prgm-const.cc:141-180`) becomes a summary with a
   dump/verify surface; the kernel-single-TU and crt0-benign axioms
   become *checked properties* of the summary (an unrooted or
   multi-root image fails closed exactly as `compute_executable_
   closure` already does, `crosscall.cc:1605-1623`).

**COMPATIBILITY.** CLASS-I. Summaries must reproduce the rescans'
verdicts (assertion phase per conversion). The asm-parser tightening is
CLASS-I *conditional on the census being clean*; a dirty census
re-classifies that sub-step. No widened admission in this item.

**GATES + PROOFS.** Per-conversion verdict-equality assertions; the
mop-form caller-template hang class (silicon-witnessed, `:79-84`) keeps
its refusing behavior on the near-miss twins. Existing refusals
touched read-only: the 26+-name crosscall taxonomy, `mop-config-epoch`,
`tu-closure-unrooted` class. New names: `ipa-summary-stale` (a summary
consulted after a body mutation invalidates it — enforced by cgraph
hook wiring, tested by a twin).

**EFFORT + RISK.** L. Top failure mode: summary invalidation bugs
(stale facts after another tt pass mutates a body) — mitigated by hook
wiring + the stale-refusal belt + the fact that all consumers are
already fail-closed. Rollback: per-conversion revert; rescans stay
`#if`'d one pin.

**DEPENDENCIES.** None hard. Soft: #1. Unlocks (stage-B, later lanes):
multi-site crosscall admission, mop multi-epoch.

**ACCEPTANCE.** No tt pass reads another function's body outside a
summary interface; `mop_classify_asm` is deleted; `tu_facts` has a
dump/verify surface; corpus byte-identical x3.

---

## 4. BOARD-RESIDUAL ATTACKS

These are the three residual attacks the roadmap names (§(d) of
COMPILER-AUDIT-REPORT.md). They are CLASS-B lanes that *consume* the
items above; none carries new soundness theory beyond what its items
provide. Floor facts from the conf PIN HISTORY: trig +0.63
CONSTRAINED-FLOOR ("the last slot needs a rename with no free LREG
(8-LREG wall) or a value-changing circuit restructure — named
successors", entry 42); addrsqrt +0.98 CERTIFIED ("named successors =
licensed/fresh-source CC-dance restructure below 24 words, or an
8-LREG rename mechanism", entry 45); softplus WIN −2.95 with hand-arm
gap +0.09 ("last word = walk candidate-ranking polish", entry 44).

### R1 — The 8-LREG rename attack (trig +0.63, addrsqrt +0.98, softplus-class collisions)

**DESIGN.** Composition lane, not a new mechanism: #7's chain renamer
is the engine (the pass itself framed the problem as regrename's
du-chain problem); #10 prices the changed pressure peak (a rename that
reclaims a register moves the peak — precondition, not accessory);
#12 lets rename-enabled schedules bid in the same cost API as
unrenamed ones (AUDIT-cost-model-refusals.md impr.5); #5's MVE is the
loop-shape consumer (rename supply for kernel copies); #9's discovery
runs over the renamed, collision-free stream (more/longer maximal
repeats). Attack order: (a) trig — the certified last-slot rename
(interior region, storage collision on L5-class fillers); (b) addrsqrt
— rename at walk pressure 7/8 with PRGM L12–L14 exhausted (the cert's
first-named successor); (c) softplus-class storage collisions where
fill/pairing is blocked by reuse, not true pressure (the audits' causal
reading: collision, not pressure).

**COMPATIBILITY.** CLASS-B. Rides #7's flag plus the consumer flags;
no new mechanism flag unless a composition gate is needed
(`-mtt-tensix-optimize-rename-bidding` if pricing integration needs
its own attribution leg).

**GATES + PROOFS.** Same-leg silicon on each target row, matrix
monotone, causal ON/OFF attribution; the constrained-floor certs are
the acceptance oracles — each row either flips or re-certifies with
the rename in the shape census (certs name the successor explicitly,
so a re-cert with rename-present is a genuine closure, not a dodge).

**EFFORT + RISK.** M once #7/#10/#12 exist (the lane is composition +
measurement). Top failure mode: the rename exists but the schedule
consumer can't cash it (fill still refuses beyond the audited latency
window) — then the cert re-certifies honestly; that is an acceptable
closure by the zero-loss-or-cert discipline. Rollback: flags off.

**DEPENDENCIES.** Hard: #7 (+its dep #4), #10, #12. Amplified by: #5,
#9.

**ACCEPTANCE.** All three certified floors (trig +0.63, addrsqrt
+0.98, softplus-class collision rows) are either flipped on silicon or
re-certified with the rename mechanism present in the census — the
"8-LREG rename" successor named in the certs no longer names missing
machinery.

### R2 — Licensed CC-dance restructures

**DESIGN.** Lands on #14 (the CC-region tree is the infrastructure the
restructure needs — COMPILER-AUDIT-REPORT.md §(d)). Two widenings, each
under a new proof artifact of the established `tt/proofs` contract
shape (exhaustive sweep vs the pinned simulator, class census, SHA
commitments, rule-cites-RESULT):

1. Widen `cc_narrowing_modifier_p` (`gimple-rvtt-invariant.cc:
   1016-1035`) — admit additional structured modifiers into the
   CC-restore proof, each certified by its own sweep directory.
2. Widen ccmask's EQ/NE refusal (`gimple-rvtt-ccmask.cc:409-412`) —
   the strict-direction folds exist (proofs in
   `tt/proofs/ccmask-direction-complete/`); the EQ/NE class needs its
   own 2^32-per-direction sweep; NOT-EQUAL rows become standing named
   refusals scoped by divergence class, EQUAL rows license the fold.

Target: the addrsqrt cert's second-named successor ("licensed/
fresh-source CC-dance restructure below 24 words") — restructuring the
row's CC dance to cut delivered words below the 24-word delivery-paced
shape.

**COMPATIBILITY.** CLASS-B, licensed where value-changing (per-class
default-off tokens in the licensed-folds style; byte-identical-off
including the standing named refusal), plain `Init(0)` where bit-exact
(EQUAL sweep rows need no license, only the proof citation — the
int-abs/int-not precedent).

**GATES + PROOFS.** New tt/proofs directories per widened class
(pinned-sim provenance, exhaustive domain, RESULT EQUAL or scoped
refusal); #3's RESULT-hash build check covers the new tables
automatically; twins per widened modifier with near-misses refusing by
name. Existing refusals touched: the ccmask EQ/NE refusal (becomes
class-scoped), `cc-narrowing-*` names.

**EFFORT + RISK.** M–L (sweeps are compute, the mechanism is #14's).
Top failure mode: the sweep comes back NOT-EQUAL on the classes the
restructure needs — then the refusal stands, correctly, and the row
keeps its cert; that outcome is a valid closure. Rollback: tokens off.

**DEPENDENCIES.** Hard: #14 (stage B). Soft: #1, #3.

**ACCEPTANCE.** addrsqrt's CC-dance successor is executed: the row
flips, or every candidate restructure class is proof-swept with each
NOT-EQUAL class carried as a standing scoped refusal — the successor
line in the cert is discharged either way.

### R3 — Trig/softplus floor closures (applications, not mechanisms)

**DESIGN.** Per COMPILER-AUDIT-REPORT.md §(d): neither floor needs new
proofs — both need the arbiter and the rename. Trig = R1(a) plus,
independently, the licensed circuit restructure named in its cert
(muli+add → fused mad = #8's license family) as the second arm.
Softplus = #13's stage B applied to the EL-vs-park ordering history:
the +0.09 hand-arm residual is priced-arbitration territory
("candidate-ranking polish"), i.e. the arbiter's acceptance row.
This lane is the *bookkeeping closure*: it re-attacks both rows once
its feeder lanes land, books or re-certifies, and updates the floor
certs so the successor names point at shipped machinery.

**COMPATIBILITY.** CLASS-B (measurement lane; any new fires belong to
the feeder flags).

**GATES + PROOFS.** Same-leg silicon, triple-rep, matrix-monotone; cert
edits follow the established cert-note append discipline (the pin-45
addrsqrt precedent: board cell unchanged, cert note appended).

**EFFORT + RISK.** S–M as a lane (its cost lives in its feeders). Top
failure mode: feeder slippage stranding the lane — schedule it in the
last wave with explicit feeder gates.

**DEPENDENCIES.** R1 (trig), #13 stage B (softplus), #8 (trig's
licensed-restructure arm).

**ACCEPTANCE.** Zero board rows whose cert names a successor mechanism
that does not exist in-tree; trig and softplus specifically re-attacked
with the feeders' machinery in the census.

---

## 5. DEPENDENCY DAG AND WAVE PLAN

### 5.1 DAG (hard dependencies only; soft deps annotated in-item)

```
#1  ──────────────────────────────► (soft: everything)
#2  ─► (stage B widens replay/mop admission)
#3  ─► (none)
#4  ─► #7
#6  ─► (none)
#9  ─► (none; amplifies R1)
#10 ─► #5, #7, #8, #13, R1
#11 ─► #5
#12 ─► #13, R1
#14 ─► R2
#15 ─► (none; unlocks later multi-site/multi-epoch lanes)
#5  ─► R1 (amplifier), crp retirement
#7  ─► R1
#8  ─► R3 (trig licensed arm)
#13 ─► R3 (softplus arm)
R1, R2 ─► R3
```

### 5.2 Waves

Wave discipline: lanes within a wave are pairwise independent
(different files or CLASS-I with disjoint sweeps); CLASS-I lanes may
share a pin (the batch-merger precedent); CLASS-B lanes take one pin
each with their own ceremony. Pin cadence estimates assume the
established cadence of 1–2 lanes per pin cycle.

**WAVE 1 — quick wins, no dependencies (4 lanes, ~2–3 pin cycles)**

| lane | item | class | ships |
|---|---|---|---|
| W1-A | #1 refusal registry | CLASS-I | rvtt-refusals.def + rvtt-refuse + opt-info routing + 8 family sweeps + generated catalogue |
| W1-B | #2 trips facade | I then B | rvtt-trips facade, dual-oracle census (stage A); oracle flip + widening ceremony (stage B, may slip to wave 2) |
| W1-C | #3 generated tables | CLASS-I | storefold-verdicts.def + RESULT-hash check; will_fuse_p generator; crf plan interpreter |
| W1-D | #6 Briggs coalescing | CLASS-B | lreg-coalesce flag, erfinv witness, spill-census delta |

Expected pins: one CLASS-I batch pin (W1-A + W1-C + W1-B stage A), one
CLASS-B pin (W1-D), one for W1-B stage B if the divergence census is
clean early.

**WAVE 2 — infrastructure unification (6 lanes, ~4 pin cycles)**

| lane | item | class | ships |
|---|---|---|---|
| W2-A | #10 pressure engine | CLASS-I | rvtt-pressure + 3-mirror retirement |
| W2-B | #11 timing engine | CLASS-I | rvtt-timing + 5-simulator consumer sweeps |
| W2-C | #12 delivery-cost API | CLASS-I | rvtt-delivery-cost + pricing/comparator dedup + MODEL SEAMS closure |
| W2-D | #4 attribute migration | CLASS-I(+re-freeze) | planner-oracle re-freeze; effect attributes; one word-fact table |
| W2-E | #14 CC-region tree | I then B | tree + query conversions (stage A); generality flag ceremony (stage B) |
| W2-F | #9 suffix discovery | I then B | automaton + superset assertion (stage A); maximal-repeat admission (stage B) |

CLASS-I halves batch into ~2 pins; #14/#9 stage-B ceremonies take a
pin each.

**WAVE 3 — classical techniques on the unified substrate (5 lanes, ~4–5 pin cycles)**

| lane | item | class | ships | gated on |
|---|---|---|---|---|
| W3-A | #5 IMS + MVE | CLASS-B | -optimize-ims; trig/addrsqrt proving ground; crp parity census | #10, #11 |
| W3-B | #7 du-chain regrename | CLASS-B | rename-chains flag + rename service; old-pass subset census | #4, #10 |
| W3-C | #13 placement arbiter | I then B | shadow arbiter + disagreement census (stage A); priced-placement flag + board re-measure (stage B) | #10, #12 |
| W3-D | #8 accumulator splitting | CLASS-B | reassoc-loop-carried token + twins | #10 |
| W3-E | #15 IPA summaries | CLASS-I | 3 summary conversions; asm-parser retirement | (none — schedulable in wave 2 if lane supply allows) |

**WAVE 4 — board-residual attacks + retirements (3 lanes, ~3 pin cycles)**

| lane | item | ships | gated on |
|---|---|---|---|
| W4-A | R1 rename attack | trig/addrsqrt/softplus-collision re-attacks; flip-or-recert | #7, #10, #12 (amp: #5, #9) |
| W4-B | R2 CC-dance | new proof sweeps; narrowing/ccmask widenings; addrsqrt CC arm | #14-B |
| W4-C | R3 floor closures + retirement ceremonies | cert updates; crp retirement if #5 parity held; single-shape rename pass retirement | R1, #13-B, #8 |

**UPSTREAMING TRACK** — runs parallel from the end of wave 1 (U1 is
satisfied by W1-A); see §7. It never blocks a wave.

### 5.3 Total

15 roadmap items + 3 residual attacks + 6 upstreaming steps = **24
planned units** across 18 implementation lanes in 4 waves plus a
parallel upstreaming track. Estimated pin envelope: ~13–15 pin cycles.

---

## 6. ACCEPTANCE LEDGER (one line per item)

| item | acceptance (measurable) |
|---|---|
| #1 | 0 refusal fprintf sites; `-fopt-info-missed` live; catalogue generated; corpus x3 identical |
| #2 | legacy trip prover + mop duplicate deleted; divergence census empty; ≥1 new counter shape adjudicated |
| #3 | verdict tables generated; RESULT-hash build check red/green proven; hand mirrors deleted |
| #4 | effect_overrides/lane_gated_consumers exist only as generated attributes; 4 word-classifiers → 1 table |
| #5 | trig floor beaten or re-certified under IMS; crp parity census complete |
| #6 | erfinv IP-2 witness folds or re-certifies; spill round-trips strictly fewer, zero new losses |
| #7 | trig re-attacked at certified floor: +0.63 flips or floor re-certified with rename in shape census |
| #8 | `reassoc-loop-carried-underived` cleared on licensed+affordable chains; ≥1 silicon row booked/certified |
| #9 | O(N²) discovery deleted; stage-A identity held; ≥1 formation legacy discovery could not enumerate |
| #10 | 3 pressure mirrors deleted; capacity constant single-sourced; corpus x3 identical |
| #11 | 5 timing simulators → 1 engine; corpus x3 identical |
| #12 | PUSH/SLOT arithmetic single-sourced; MODEL SEAMS deleted; corpus x3 identical |
| #13 | softplus +0.09 flips or re-certifies priced; cut lines deleted; no board regression |
| #14 | reordered-layout twin flips FUTURE-VERDICT under stage-B flag, bit-golden |
| #15 | no cross-function body reads outside summaries; asm parser deleted; corpus x3 identical |
| R1 | all rename-successor certs discharged (flip or re-cert with mechanism present) |
| R2 | addrsqrt CC-dance successor discharged (flip or proof-swept scoped refusals) |
| R3 | zero certs naming nonexistent successor machinery |

---

## 7. UPSTREAMING APPENDIX — staged submission plan

Sequenced by credibility, not size (COMPILER-AUDIT-REPORT.md §(e)).
General prerequisites for every step: FSF copyright assignment or DCO
per current GCC policy; patches against trunk in a stage-1 window;
each submission carries its own testsuite (target-independent tests
where the code is target-independent); no submission references
internal lane/pin machinery.

**U1 — Prerequisite: opt-info routing (item #1).**
Prereq: none. Effort: covered by W1-A. Acceptance: the tt refusal
stream is `dump_printf_loc`-based, so nothing submitted later carries
the bare-fprintf idiom upstream review rejects on sight
(AUDIT-interprocedural.md impr.2).

**U2 — First submissions: spill-diag hook + BnB/prera pressure pass.**
(a) `TARGET_DIAGNOSE_UNSPILLABLE_CLASS`: hookize
`rtl-rvtt-spill-diag.cc`'s mode-keyed detect / named `error_at` /
neutralize / ICE-backstop structure; the hook surface is the
unspillable class + mode set + relief-flag names
(AUDIT-regalloc.md impr.5a). Any accumulator/predicate-file target
that ICEs today is the motivating case in the submission.
(b) The `rvtt-schedule.h`/`rvtt-bnb.cc`/`rvtt-lpsolve.cc` solver stack
plus the prera pass as a generic transactional pre-RA pressure
re-schedule — haifa's SCHED_PRESSURE_MODEL made exact and
transactional, which the pass's own header already frames
(`rtl-rvtt-lp-schedule-prera.cc:36-59`); hook surface =
`REGISTER_CAPACITY` per class + a region-admission predicate. The
solver carries zero IR pointers by design, so it extracts cleanly.
Prereqs: U1; genericized option names; the lp_solve cross-check made
`--with-` conditional upstream-style (it already is). Effort: S–M (a),
M (b). Risk: review taste on adding an opt pass — mitigated by the
byte-identical-across-build-configs guarantee and the independent
recount belts, which are the submission's selling points.

**U3 — Second wave: crossloop-hoist + launch-flatten.**
(a) crossloop-hoist as a LICM extension behind
`TARGET_LOOP_BODY_HOIST_INERT_P (loop, mask)` — the one pass the
interprocedural audit says could land essentially as-is; prereq: move
the region scan out of crosscall.cc (the layering wart,
`crosscall.cc:3242`) — a small #15-adjacent refactor that wave 3
delivers anyway. (b) launch-flatten as a GIMPLE analogue of
`targetm.loop_unroll_adjust` with pragma-bypass semantics — the pass
is already annotation-only (`loop->unroll` request); hook surface =
target-priced body words + per-loop/function budgets. Prereqs: U1, U2
landed or in review (credibility ladder). Effort: M + S–M.

**U4 — The deletion play: licensed reassoc via internal fns.**
Lower plain-mod FP/int builtin ops to internal fns (or vector GIMPLE
binops) carrying commutative/associative properties until late
lowering, so upstream `tree-ssa-reassoc.cc` +
`targetm.sched.reassociation_width` performs the rebalance under stock
`-fassociative-math`, deleting ~500 lines of family machinery and
leaving only a window-barrier predicate target-side
(AUDIT-licensed-folds.md impr.5). Prereqs: #14 (barrier predicate as a
clean query), #8 landed (so the loop-carried arm's licensing story is
settled before the IR changes under it), full twin coverage ported.
Effort: L (IR change). Risk: highest of the track — the IR
representation change can perturb every downstream tt pass; run as its
own CLASS-B-equivalent internal ceremony before any upstream mail.

**U5 — The ambitious pair: `targetm.replay` + relational-web binding.**
(a) `targetm.replay` hook family — hardware instruction memoization
(GCC lacks it; MachineOutliner only approximates it): target supplies
`classify_insn` (safe/owner/boundary), record/launch emitters, buffer
geometry, cost vector; the O1 dominating-deliverer obligation stays
generic, O2/O3 wedge rules are target-supplied
(AUDIT-replay-formation.md impr.5). Prereqs: #9 (discovery separated
from Tensix), #12 (cost vector = one module), #2 (stock niter). Effort:
XL. (b) The pre-IRA relational-alternative web binding hook — fixes
the documented IRA defect (per-operand alternative costing loses
relational alternative sets, `rtl-rvtt-lp-alloc.cc:1937-1963`), with
the DSATUR-completion certificate as the API's soundness contract; the
DSATUR allocator itself is deliberately NOT submitted (it is a
certificate generator, not an allocator). Prereqs: U2 landed (shares
reviewers/vocabulary). Effort: L.

**U6 — Publish, don't upstream.**
The twin-test/FUTURE-VERDICT methodology, the refusing-default
attribute discipline, and the proof-scoped license architecture are
policy any target could adopt — documentation and artifact material,
not patches. Prereq: none; deliverable: a methodology document in the
tree (tt/ already carries NOTES-*.md precedent). Effort: S.

---

## 8. CONSIDERED AND DEFERRED (not in the waves)

- **Real DFA (`define_insn_reservation` per insn + `define_bypass`)** —
  AUDIT-cost-model-refusals.md impr.4. Deferred as **data-blocked**:
  it needs the tuned per-CPU latency values the cost model itself IOUs
  ("F1.3 will supply tuned per-CPU values", `rvtt-cost.md:11`). #11
  is designed so this becomes a representation swap inside one engine
  when the data lands; planning it now would schedule a measurement
  campaign, not an engineering lane.
- **Exact-solver seam widening (latency edges + II variable in the BnB
  model)** — AUDIT-scheduling.md impr.4. Deferred to post-#5: the IMS
  provides the cyclic candidates; widening the solver to adjudicate
  IMS repair choices and replay selection is a quality upgrade with no
  acceptance row of its own yet. Revisit after W3-A's parity census.
- **mop-form multi-epoch ownership** (`mop-config-epoch` residual) and
  **crosscall multi-site admission** — both unlocked by #15 but
  scheduled as stage-B lanes only when a board row demands them; no
  current loss row names either as successor.
- **Self-calibration of `MIN_SEQUENCE = 4`** ("no silicon point
  separates 3 from 4", `rtl-rvtt-replay.cc:71-75`) and the WH
  `drained_frontend_window` witness — one-measurement items, queued as
  witness errands on any wave-4 silicon session rather than lanes.

Nothing in the audit's roadmap was ruled unplannable.

---

*Plan authored from the pin-48 source and the eight family audits; no
builds were run for this document. Where a wave lane discovers the
source has moved past a cited line, the audit citation governs intent
and the lane re-anchors before implementation.*
