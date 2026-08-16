# The generic SFPLOADMACRO macro planner

Reconstructed design document (WP8).  The original DESIGN.md and WP
notes existed only on the retired machine; this document is rebuilt
from the implemented code, the capability-table provenance audit
(`gcc/config/riscv/tt/NOTES-wp6-prep.md`), and the handoff's
branch-contents record.  It is the authoritative description of the
planner as shipped at the end of WP8.

## 0. Mission and the non-negotiable rule

The planner exists so that ordinary typed C++ compiles to SFPLOADMACRO
code competitive with handwritten LLKs through **reusable generic
mechanisms**.  The absolute rule (standing user directive): **no
hardcoding in compiler decision logic** — no operation names, no exact
opcode calendars, no coefficient/immediate fingerprints, no magic
instruction words.  Raw encodings live only in target
encoding/capability tables (`rvtt-macro-tables*`) and in test
expectations.  Refusals never mutate code and are byte-identical to
flags-off output.  Genericity is proven per commit by
renamed-equivalent tests, varied-constant tests, near-miss refusals,
and at least two unrelated shapes.

The planner replaced, and at WP8 finally deleted, the quarantined
exact-calendar pass `rtl-rvtt-loadmacro.cc` (its opt-in flags
`-mtt-tensix-{analyze,emit}-loadmacro` now error on use).  Byte-parity
oracles minted from that pass before deletion are frozen in
`gcc/testsuite/g++.target/riscv/tt/tensix/oracles/`.

## 1. The seven layers

The planner is a layered pipeline; every layer consumes only the
previous layers' vocabulary, never IR shape names.

1. **Typed effect attributes** (`rvtt-cost.md` attribute families,
   `rvtt-effects.{h,cc}`).  Every Tensix instruction pattern is either
   *audited* — carrying subunit, LREG read/write operand positions, CC
   effect, configuration effect, and Dst/RWC counter effect — or
   *opaque* (the refusing default).  `rvtt_insn_effects` is the ONLY
   classification vocabulary the later layers may use; recog-code
   comparisons are permitted solely to reach an admitted instruction's
   operands.

2. **Region discovery** (`rvtt-macro-region.cc`, Layer 2).  A *row* is
   a dataflow-closed slice from its Dst loads to its Dst store (every
   member feeds the store; no external LREG live-ins).  Rows group into
   regions when pairwise isomorphic to the first row under a value map
   with a uniform typed Dst stride.  Pure-RWC counter effects (typed
   TTINCRWC / TTSETRWC / face advances) terminate rows and separate
   *runs* inside one region; pure CC writes between rows are ambient
   enables.  Configuration writers and CC-writing value events refuse
   at the event itself (`row-config-write`, `cc-template-unsupported`);
   opaque effects are hard boundaries.  Since WP8, regions may have a
   SINGLE row (whether one launch amortizes its configuration is a
   Layer-6 question), and a self-looping block marks a loop-body
   region.

3. **DAG scheduling** (`rvtt-macro-sched.cc`, Layer 3).  Dst accesses
   with equal typed address operands share one issued carrier (launch);
   the carrier-grouping search is a deterministic two-candidate
   sequence (maximal sharing, then store demotion) and the first
   candidate whose descriptor proves is committed.  Launched sequence
   events are non-Dst value operations the tables can host; per-event
   programmed delays come exclusively from the tables' proven sequence
   programs (`DELAY_UNKNOWN` never validates a derived schedule).  The
   typed Dst stride is absorbed into the last carrier only when the
   tables' address-modifier machinery covers the delta.

4. **Capability tables** (`rvtt-macro-tables.{h,cc}`,
   `rvtt-macro-tables-{bh,wh}.def`, Layer 4).  The DESIGNED HOME of
   every raw SFPLOADMACRO word: launch/SETC16/SFPCONFIG field layouts,
   address-modifier slot programs, proven whole-word sequence programs,
   proven misc words, hidden template LREG writes, and reference
   descriptors (audit data, never decision input).  QSR is
   intentionally table-absent (`target-macro-encoding-unproven`).

5. **Descriptor synthesis** (`rvtt-macro-desc.cc`).  Selection is keyed
   by DERIVED event structure — per-macro subunit lists, store
   placement, and the admitted source instructions' opcode bytes
   through the retained TT_OP encoding tables — never by shape or
   operation names.  Template words are field-packed
   (`encode_template`) from the admitted source operands
   (`TR_FIELDS_FROM_SOURCE*`) or from table data (`TR_TABLE_FIELDS`:
   TT_OP opcode byte, proven fixed mod1, positional routing selector,
   imm12 from the typed source immediate with a hard range refusal).
   Sequence and misc words are resolved from the matched program's
   provenance labels against the capability tables — the labels are
   opaque indices into proven table rows; no raw descriptor word lives
   outside the tables and the test expectations.

6. **Profitability** (in `rtl-rvtt-macro-planner.cc`).  Derived from
   configuration and drain costs — no row thresholds anywhere.
   Straight-line: every run independently amortizes the full
   configuration prefix.  Loop bodies: the prefix is paid once in the
   preheader and weighed against the profile body/preheader count ratio
   in count space without rounding (`trip_weight`).  The frozen pass's
   break-evens (BH 7 / WH 8 rows for minmax) are DERIVED, and stored in
   the tables only as regression expectations
   (`reference_breakeven_rows` is never read by the planner).

7. **Verification and formation** (`rvtt-macro-verify*`,
   `rtl-rvtt-macro-planner.cc`).  A Layer-7 differential verifier
   re-derives expectations from the region's explicit facts; any
   mismatch is a descriptor refusal.  Formation proves function-global
   configuration ownership (calls, asm, and typed accesses to owned
   config destinations refuse), all planner-owned physical LREGs dead
   after the region, the all-lanes lane proof, and stride absorption,
   then emits: configuration prefix (ambient enable, owned SETC16
   address-modifier program, descriptor words through an owned LREG),
   per-row launch calendar, and the drain.  Refusal paths never mutate.

## 2. WP8 additions

* **Single-row regions** — discovery admits one-row regions;
  straight-line single rows refuse `unprofitable` (the quarantined pass
  formed them unconditionally; the oracle store records this
  divergence-by-design — a macro that costs ~19 issue slots against 6
  explicit ones is not a win the planner will claim).
* **Loop-body regions with preheader configuration.**  Structural
  preheader = the loop header's unique external predecessor with a
  single successor (which proves at least one trip, so hoisting the
  ambient enable is not a zero-trip CC change), plus whole-body Tensix
  ownership (every issue in the body belongs to the region).  Named
  refusals: `loop-preheader-unproven`, `zero-trip-preheader-unproven`,
  `loop-body-not-owned`.
* **Trip-weight profitability** — see Layer 6 above.
* **Lane-proof relaxations** — the all-lanes proof is required at the
  region's FIRST row only (members cannot write CC, so the entry state
  holds), and a loop-body region whose enable was written once outside
  the loop takes the proof from the dominating trailing enable on the
  preheader's unique-predecessor chain (only scalar code between it and
  the loop entry).
* **Signbit parity** (unary load/shift/cast/store).  The planner forms
  the loop shape byte-identically to the quarantined oracle: preheader
  SFPENCC + owned SETC16 + four descriptor words, one launch per row,
  drain 3.  The shift immediate packs the template imm12 field (any
  encodable constant; out-of-range or run-time amounts refuse); the
  explicit shift-mode → template mapping is a single proven pair pinned
  in the program table (NOTES 9(e)).
* **Cast-round parity** (unary load/cast/round/store).  Byte-identical
  to the quarantined oracle on BH and WH.  WP8 corrected the round
  template's mod1 source to the explicit instruction's architectural
  instr_mod1 operand (verified by decoding the emitted explicit word
  0x8e000001); the imm8 operand routes through the imm12 packer whose
  0x8e nonzero refusal keeps unproven immediate forms out.
* **Typecast four-region descriptor sharing.**  Typed TTSETRWC face
  transitions are pure-RWC run separators, so four faces discover as
  ONE region sharing one configuration: unrolled (32 rows / 4 runs, one
  config, per-face drain, transitions preserved in place) and dynamic
  (the face loop's body with the prefix hoisted to the preheader — the
  one-configuration, 32-launch schedule the Typecast blocker demanded).
  The WH bank-base obligation is discharged by the owned dual-slot
  SETC16 program the tables carry (slots 2 and 6; the launch's two-bit
  selector maps through unencoded incoming Base state).
* **Where → named refusal.**  A CC-writing value event inside a row
  slice needs a CC-manipulating instruction template; no proven
  CC-template program exists, so the predicated-select shape refuses
  `cc-template-unsupported` byte-identically (the quarantined pass's
  0x706-misc select emission — which failed TTNN Where integration
  against the simulator's 0x770 protocol — is gone with the pass).
* **Deletion.**  `rtl-rvtt-loadmacro.cc`, its registration, its md
  patterns, and its flags (now erroring) are deleted; the oracle store
  under `testsuite/.../oracles/` (bodies, mint script, manifest) is the
  permanent parity record.
* **Region-scoped configuration ownership for loop-body regions** (the
  cross-function increment, post-WP8).  The function-global ownership
  proof can never hold on a real kernel: the enclosing function carries
  opaque init/dataflow code (raw MMIO instruction pushes, asm, typed
  config accesses), so the WP8 dynamic typecast form only ever fired on
  synthetic whole-function tests while the real four-face loop refused
  `config-ownership-unproven`.  When the global proof fails and the
  region is a loop body, formation now attempts the scoped window: the
  proven structural preheader (unique guarded external predecessor,
  zero-trip obligation, whole-body Tensix ownership — the existing
  `loop_region_preheader` proofs) plus a body scan proving every
  instruction is either region-owned or provably inert scalar code (no
  call, no asm, no Tensix issue, no volatile memory reference — the
  shape of every raw issue the typed vocabulary cannot see).  The
  configuration prefix materializes at the preheader's TAIL — the
  compiler-owned insertion point after the last reachable foreign owner
  — which dominates every trip's launches, and no path from it to the
  final drain contains another owner.  Foreign owners before the
  insertion point are overwritten by the prefix; code after the loop
  exit runs beyond the descriptor's lifetime, exactly as when the
  planner forms inside an out-of-line callee invoked from an opaque
  caller (the shipped straight-line contract).  Success prints the
  info line `Macro-planner config-ownership: loop-scoped window
  (preheader tail dominates every launch)`; every scoped failure keeps
  the established `config-ownership-unproven` name and the bytes
  explicit.  On the real typecast-shaped kernel (the opt-in
  TYPECAST_TYPED_RWC_BOUNDARY probe of the blocker doc) this produces
  ONE descriptor configuration per tile in the face-loop preheader and
  eight alternating-VD launches per face — the one-configuration,
  32-launch dynamic tile schedule the blocker demanded, on BH and (with
  the dual-slot bank-base SETC16 program) on WH.  The straight-line
  contract, non-loop regions, and every previously-refusing shape are
  unchanged.

## 3. Why the proven-program tables carry whole sequence/misc words

The bit-level format of the sequence words and of misc bits above 3:0
is architecturally UNESTABLISHED (NOTES-wp6-prep.md §9(b)/(c)); the
per-event delay fields of several proven programs are undocumented
(§9(g)).  Until an independent architectural reference or a validated
4-sub-unit timing model exists, sequences and misc words are proven
whole-word capabilities: `encode_sequence` is a whole-word lookup over
the proven programs and refuses everything else, and no encoder is
derived from the suggestive-but-unverified bit observations.  Deriving
sequence words from scheduler events alone is not even possible for
every proven program — the minmax macro-1 program contains a
macro-internal transient-copy event with no derivable source
instruction — which is exactly why descriptor synthesis references
proven table rows by provenance label after a purely structural match.
The CRAQ-validated envelopes (e.g. `uniform_mode_required`, the proven
store modes, the pinned shift-mode pair) bound what the whole-word
programs may be applied to; structurally-identical shapes outside an
envelope refuse `descriptor-program-unproven` (near-miss tests pin
this per program).

## 4. Refusal vocabulary (stable dump API, append-only)

Discovery: `row-opaque-effect`, `row-not-closed`,
`cc-template-unsupported` (supersedes pre-WP8 `row-cc-write`),
`row-config-write`, `row-not-isomorphic`, `row-stride-mismatch`,
`row-live-through`.
Schedule: `event-delay-unproven`, `sequence-encoding-unproven`,
`template-capacity-exceeded`, `port-conflict`, `latency-violation`,
`target-macro-encoding-unproven` (table-absent CPU).
Descriptor: `descriptor-program-unproven`,
`descriptor-encoding-failed`, `descriptor-verification-failed`.
Formation: `config-ownership-unproven`, `planned-lreg-live`,
`all-lanes-proof-missing`, `loop-preheader-unproven`,
`zero-trip-preheader-unproven`, `loop-body-not-owned`,
`unprofitable`, `stride-not-absorbed`.

## 5. Flags

`-mtt-tensix-macro-planner` (formation), `-mtt-tensix-macro-planner-analyze`
(report-only), `-mtt-tensix-macro-planner-verify` (differential
verifier; forced under internal checking),
`-mtt-tensix-macro-planner-verify-corrupt-template` (verifier
self-test).  All default off; default codegen is byte-identical to the
pre-planner compiler (corpus A/B re-verified at WP8: 721 objects across
bh/wh/qsr32, all identical).

## 6. Known limitations and carry-forwards

* Schedule-level `event-delay-unproven` (NOTES 9(g)) does not block
  formation when the whole-word program is proven end to end; the
  proven-calendar drain (3 slots) applies.  A 4-sub-unit timing model
  would replace this with per-event validation (the sim-side delayed
  event model is the intended reference).
* The verifier's template/sequence/misc expectations derive from the
  same program table as synthesis; wrong-table-entry detection rests on
  the frozen-oracle byte-parity suite (launch words and SETC16 programs
  are re-derived independently).
* Function-global configuration ownership is conservative;
  path-sensitive refinement through `rvtt-macro-ownership` remains a
  documented widening that needs fresh tests.
* Loop-body regions are single-BB self-loop shapes; multi-BB loop
  bodies and regions spanning control flow are out of scope.
* A preheader that IS the function entry block refuses
  (`loop-preheader-unproven`) rather than splitting the entry edge.
* The ambient-enable proof accepts any pure CC write as the enable
  shape; that the written state is all-lanes is carried by the copied
  instruction itself (the emission replays the exact enable pattern),
  not re-proven architecturally.
* Typecast four-region sharing is compiler-complete and byte-pinned in
  DejaGnu, but paired CRAQ correctness on the real TT-Metal typecast
  node has not been run in this lane (simulators are read-only for WP8;
  no sim change was needed).  Blocked-on-integration, not on sim
  coverage.
