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
  The WH bank-base obligation is discharged by the Base=1 SFPU
  platform contract (the LLK start/done bracket pins
  ADDR_MOD_SET_Base=1), under which the tables' owned SINGLE-slot
  SETC16 program (physical slot 6, regs 19/29/54) is the launch's
  only reachable bank; the historical dual-slot program wrote the
  base-0 bank -- LLK's live ADDR_MOD_2 -- and was adjudicated a
  miscompile (sfpi-gcc 2a0ba1e6602; laneAJ-evidence-20260817).
* **Where → named refusal.**  A CC-writing value event inside a row
  slice needs a CC-manipulating instruction template; at WP8 no proven
  CC-template program existed, so the predicated-select shape refused
  `cc-template-unsupported` byte-identically (the quarantined pass's
  0x706-misc select emission — which failed TTNN Where integration
  against the then-whitelisting simulator's 0x770 protocol — is gone
  with the pass).  SUPERSEDED at WP9: see Sec. 2a — the simulator now
  executes descriptors architecturally, and the proven CC-template
  programs form the select class; unproven CC forms keep named
  refusals (`cc-template-unsupported`, `cc-template-unproved`,
  `cc-enable-unproved`).
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
  the single-slot Base=1 SETC16 program) on WH.  The straight-line
  contract, non-loop regions, and every previously-refusing shape are
  unchanged.

## 2a. WP9: the CC-template extension (predicated select / TTNN Where)

Rows whose dataflow carries CC-writing events -- the select class:
predicate write, lane-predicated merge, all-lanes restore, store --
previously refused `cc-template-unsupported`.  WP9 makes proven
CC-write template events representable end to end:

* **Discovery** admits CC writers in exactly two structural roles: a
  predicate DEFINITION (a value event reading LREGs and writing CC
  with NO LREG result -- the SFPSETCC-on-register class; value-producing
  CC writers such as the SFPIADD CC mods keep `cc-template-unsupported`,
  no proven dual-effect template exists)
  and, only after a definition, the in-row all-lanes RESTORE (a pure CC
  write proven word-exact through the shared P0 SFPENCC derivation).
  The row slice follows CC edges as well as LREG edges; a CC need
  surviving to the row entry is the sanctioned ambient-enable
  dependency.  Every other CC writer keeps its named refusal
  (`cc-template-unsupported`, `cc-enable-unproved`).

* **Scheduling** hosts the definition on its predicate-source load's
  carrier (the existing producer rule) and the restore on the
  LAST-issued load carrier; the lane-merge (a CC-reading value event
  that reads its own destination, takes one other input, both produced
  by this row's loads, result consumed by the store) is realized as
  `CC_COALESCED` -- no issued word, no template slot: the shared launch
  VD receives every payload and the post-visibility load IS the
  predicated write.  Any other CC-dependent value event in a
  predicate-writing row refuses `cc-template-unproved`.  A CC row never
  absorbs its separator: the explicit counter word is the restore's
  visibility slot.

* **The descriptor CC model** (`macro_cc_model`) derives, from the
  matched program's proven delays and two architectural facts in the
  capability tables (`cc_visibility_lag` = 1: a macro event's CC result
  is visible to issues one slot after it executes;
  `store_lane_mask_live_at_execution`: the delayed store's lane
  predicate is the LIVE CC state at the store's execution cycle -- the
  ISA's launch-latched store overrides are exhaustively Addr, the Mod0
  source, and the backdoor bit, and a CC write retiring in the store's
  own cycle is not yet visible to it), the slots that make the
  coalescing sound: pre-visibility payload < definition-visible <=
  post-visibility payload; restore visible after the predicated payload
  and by the next row's first slot; and the **restore-store race
  constraint** -- the all-lanes restore must retire STRICTLY BEFORE the
  store executes (`restore_exec < store_exec`, i.e. restore-visible
  slot <= store-exec slot with lag 1), and the store must retire before
  the next row's predicate definition executes (`store_exec < def_exec
  + ii`).  A schedule violating the race constraint refuses
  `cc-restore-store-race` -- the architectural cause of the 2026-08-17
  silicon adjudication's separator-kept mis-select, pinned by the
  corrected CRAQ delivery model (craq-sim 9f324140).  The template
  predicate SENSE is complemented exactly when the post-visibility
  payload carries the merge's live operand; the complement mapping
  (SFPSETCC mod1 bit 2, register-test class only) is capability-table
  data.  Any other failed obligation refuses `cc-template-unproved`.
  The Layer-7 verifier re-derives the whole model and re-checks every
  inequality, the race constraint included, from the exchanged slots.

* **The derived calendars.**  The ESTABLISHED (WP9) calendar is two
  launches plus one explicit payload load per row (the demoted hostless
  middle carrier): macro 0 carries the
  condition load, the SETCC template (dest 0, packed from the admitted
  source with the derived sense), and the delayed store; macro 1
  carries the post-visibility payload and the ENCC restore template
  (dest 1, table fields).  Sequence words: the frozen `select-m0`
  (SETCC d0, store d2) and `select-m1-encc` (the frozen "ENCC d0" whole
  word at the derived calendar's macro index -- the word does not
  encode the index; validated by the generic descriptor-driven CRAQ
  path).  Misc is FIELD-DERIVED: 0x700 | store mode
  (`encode_misc_select`), so varied payload modes re-derive.  VD is
  fixed at 0 (every payload flows through the shared launch VD).

  The COMPACT calendar (WP10; the production handwritten Where
  protocol's own 3-slot row, one issue slot per row cheaper) is a new
  deterministic scheduling candidate tried AHEAD of the established
  two for predicate-writing rows: the restore is hosted on the
  EARLIEST non-definition load carrier (the second launch), the
  trailing payload load stays EXPLICIT and absorbs the row stride
  through its own auto-increment address mode (the owned SETC16
  address-modifier program; the typed separator is deleted), and the
  store's data mode rides its carrying launch's mod0 (the proven whole
  misc word `select-launch-mod0`, 0x770 -- the shipped
  ckernel_sfpu_where.h init's own word).  The interval compresses to 3
  exactly because the earlier-hosted restore becomes visible in the
  next row's first slot; the same macro_cc_model inequalities prove
  it, with the additional launch-sourced-mod0 obligations: the
  definition carrier's load mode must EQUAL the store mode, and the
  absorbing explicit load must occupy the row's last issue slot (every
  other issued word's typed address is launch-latched or dispatched
  before the auto-increment executes).  Rows outside the envelope --
  a differently-typed condition (the fp16b TTNN Where selector loads
  the condition as F16b and stores U16), an uncovered stride delta, or
  a non-trailing payload -- refuse the compact candidate by name and
  fall through to the established 4-slot calendar, WHOSE DESCRIPTOR NOW
  REFUSES `cc-restore-store-race` (see the silicon adjudication note
  below).  Both sequence words and the templates are the same proven
  table rows in both calendars.  Since WP10 a schedule that names its
  own blocker never reaches descriptor synthesis; the ONE carve-out
  remains `event-delay-unproven` (Sec. 6).

* **Silicon adjudication (2026-08-17), root-caused: the 4-slot select
  calendar refuses the ARCHITECTURAL `cc-restore-store-race`.**  The
  Where silicon adjudication (Lane AD, tt-quietbox-0, BH p150; evidence
  root `~/sfpi-uplift/where-adjudication-20260817`,
  verdicts/VERDICT.md) proved on a reset-first, control-proven device
  that the formed separator-KEPT calendar (misc 0x706 class; the fp16b
  and Float32 TTNN Where rows) MIS-SELECTS on silicon deterministically
  across two independent resets -- the TRUE-branch (CC-visible) store
  slot delivers wrong data (`all_zeros` PASS, `all_ones` FAIL) -- while
  the byte-identical binaries passed the then-current generic
  simulator; the separator-ABSORBED compact calendar (misc 0x770 class;
  the Int32 rows) is silicon-CORRECT in both delivery arms (RISC-pushed
  and replay-wrapped) across the same resets.  The corrected CRAQ
  delivery model (craq-sim 9f324140, which reproduces the silicon
  verdict byte-for-byte on the adjudication ELFs) pinned the cause: the
  scheduled store's lane predicate is LIVE at execution
  (`store_lane_mask_live_at_execution` -- SFPLOADMACRO.md's
  launch-latched store overrides are exhaustively Addr/Mod0/backdoor),
  and the 4-slot calendar retires its SFPENCC restore in the SAME cycle
  as its Delay-2 store (restore exec = 2+1+0 = 3 = 0+1+2 = store exec),
  so the store executes under the SFPSETCC complement mask and leaves
  the true-branch lanes unwritten; the compact calendar retires the
  restore one cycle earlier (2 < 3) and is correct.  The descriptor CC
  model therefore refuses any schedule whose all-lanes restore does not
  retire strictly before the store executes by the stable name
  `cc-restore-store-race`, derived from the slots and proven delays
  alone, and the region falls back byte-identically to the semantic
  (planner-OFF) lowering.  This SUPERSEDES the interim structural
  refusal `cc-separator-kept-silicon-unproven` (retired): the
  architectural constraint provably subsumes it, because under the
  derivation rules every kept-separator select schedule races -- the
  established hosting rule pins the restore to the LAST of the row's
  three load carriers (issue slot >= 2, so restore exec >= 3 with the
  proven ENCC d0), the shared launch VD forces the condition load to
  slot 0 (a payload issued before the definition executes would clobber
  it, an existing model refusal), and the proven `select-m0` store
  delay 2 fixes store exec = 3 -- while a hypothetical kept-separator
  schedule satisfying the constraint would now FORM (slot-keyed, not
  structure-keyed; exercised at the unit level in
  rvtt-macro-verify-test.cc).  The fp16b/Float32-vs-Int32 split
  observed on silicon remains a consequence of which shapes can absorb,
  not a classifier.  The compact separator-absorbed candidate keeps
  forming unchanged; the fp16b condition-mode unification moves those
  rows onto the silicon-proven compact path.

* **Formation**: the ambient all-lanes proof composes with P0 -- the
  first row's local enable, the loop preheader's trailing enable, or
  (the real-LLK case, where init is opaque TTI assembly) the WP10
  MATERIALIZED ENABLE, superseding the WP9 first-row peel: the first
  row's own typed all-lanes restore (proven word-exact through the
  shared P0 SFPENCC derivation) is pattern-copied to the head of the
  configuration prefix, so every row -- the first included -- forms
  (`lane-proof=materialized-enable`; rows without an in-row proven
  restore keep `all-lanes-proof-missing`).  The license is the
  compiler's own established outermost-CC-depth contract: the row's
  SETCC/.../ENCC combine is produced by rvtt_cc's outermost-depth
  transform, which already rewrites the outermost POPC (restore the
  incoming state) into ENCC (enable all lanes) -- sound exactly because
  the architectural kernel convention pins the outermost lane state to
  all-lanes; the materialized word re-writes the state that contract
  already guarantees.  Configuration ownership is the
  ordered union of the scoped proofs: when the function-global proof
  fails, any loop-body region first tries the loop-scoped WINDOW proof
  above; a proven CC-template program the window did not prove --
  including the straight-line shapes the window never covers --
  additionally tries the WP9 REGION-SCOPED proof (prefix-to-region-end
  clean of calls/asm/owned-config accesses; foreign config before the
  prefix is dead because the prefix rewrites every consumed
  destination; foreign code after the region is tolerated under the LLK
  convention that every SFPLOADMACRO consumer programs its own
  descriptors -- a documented accepted risk carried from the frozen
  pass's select contract).  Every other shape keeps the conservative
  function-global gate.  The kept separator is re-emitted verbatim per
  row.

* **Evidence**: the real TTNN Where kernel (tt-metal
  `sfpu_ternary_test.cpp`, `ttnn_where_impl=1`) forms on BH and WH
  (7 macro rows + peeled lane-proof row, config in the face-loop
  preheader) and passes the exact-equality (`torch_equal_nan`) CRAQ
  correctness nodes on both CPUs through the simulator's generic
  descriptor-driven SFPLOADMACRO path; the launch trace shows the
  formed calendar executing with the all-lanes store latch.  The
  derived descriptor words equal the frozen select protocol's
  (templates 0x7b0000c6 / 0x8a0000d0, seq 0x13000004 / 0x00000005, misc
  0x706) with the misc StoreMod0 and the SETCC sense now field-derived.

## 2b. WP11: cross-tile prefix elision (the configuration epoch)

The measured Where replay arm (silicon promotions 2026-08-17) pays its
17-word configuration prefix once per `_calculate` call -- once per
tile -- while the handwritten kernel pays 9; under the corrected
delivery model the prefix is RISC-pushed, outside the launch run, so
every re-programmed word is real delivery cost.  Of our 17 words, the
13 descriptor words (templates, sequence words, misc through the owned
LREG) program SFPU state that provably survives from the previous tile
of the SAME kernel: nothing in the per-tile LLK bracket writes an
SFPCONFIG destination.  WP11 makes that a proof and elides them:

* **The configuration-epoch proof** (`rvtt-macro-epoch.cc`).  When a
  formed CC calendar's configuration preheader itself sits inside an
  enclosing issue loop (the tile loop), every instruction of that loop
  outside the region must be a proven NON-OWNER of the planner's
  SFPCONFIG destinations: typed effects must carry no owned config
  access and no foreign SFPU dataflow; raw `.ttinsn` words (the
  constant single-input asm form, plus the audited scalar templates:
  fence, ebreak, the pcbuf/mailbox store-load-consume roundtrip) must
  not be an owned-destination SFPCONFIG, with the opcode and field
  layout taken from the capability tables; and EVERY volatile store is
  treated as a potential RISC instruction push whose stored word must
  resolve to a 32-bit interval with a provably non-SFPCONFIG opcode
  byte (or a provably unowned destination).  The value resolver is a
  demand-driven memoized reaching-value walk over post-RA hard
  registers -- constants, lui/addi chains, scc/shift/and/disjoint-or
  composition, predecessor joins, and monotone self-loop inductions
  bounded by an equality exit (the LLK math-sync push loops); anything
  else refuses.  Calls, unrecognized assembly, and opaque Tensix
  issues refuse.  Refusal names (append-only):
  `prefix-epoch-invalidated` (an intervening owner),
  `prefix-epoch-unproven` (an unresolvable word or opaque issue),
  `prefix-hoist-preheader-unproven` (no unique external entry).  Every
  refusal keeps today's per-tile prefix byte-identically.

* **Placement.**  The descriptor words hoist to the enclosing loop's
  structural preheader; when the unique entry edge's source is shared
  control flow (the guarded tile loop), the edge is split AT COMMIT
  TIME only -- after every proof has passed, so refusal paths never
  mutate -- and the split block executes exactly when the loop is
  entered, discharging the zero-trip obligation by construction.  The
  hoisted block is self-sufficient under lane masking: a copy of the
  proven all-lanes enable precedes the lane-predicated LREG
  materialization, under the same outermost-CC-depth license as the
  materialized enable.

* **What stays per tile.**  The ambient enable (the calendar's entry
  lane state is re-established every tile -- the LLK bracket's
  LaneConfig default reset intervenes) and the owned SETC16
  address-modifier program (SETC16-visible state is reachable from
  data-plane MMIO writes the value proof cannot bound -- the pcbuf and
  mailbox pointers are runtime-loaded).  The recurring per-tile prefix
  drops from 17 words to 4 (enable + 3 SETC16); the launch calendar,
  drain, and every body word are unchanged.

* **Scope.**  CC-template calendars only (`desc.cc.active`), so every
  non-CC formed shape (minmax, signbit, typecast) is byte-identical;
  a second formed region in the same enclosing loop refuses
  automatically (its sibling's typed config writes and SFPU dataflow
  are intervening owners).  Success prints `Macro-planner
  prefix-epoch: cross-tile config invariance proven (...)` and the
  formed line gains `prefix-epoch=hoisted`.  On the real TTNN Where
  kernel (all three unified formats) the tile loop's semaphores, MOP
  programming, fences, mailbox flags, dest-offset SETC16 push
  (`0xB2010000 | dyn<<9`, opcode byte provably constant), and math
  dvalid push loops (monotone induction, opcode byte constant across
  the bounded range) all classify inert, and the 13 descriptor words
  are elided from tiles 2+.

## 3. Why the proven-program tables carry whole sequence/misc words

SUPERSEDED IN PART (timing-calendar derivation): the sequence-word bit
format and the per-event delay semantics are now ESTABLISHED by three
independent architectural sources, and calendars are DERIVED for
shapes no proven whole-word program covers — see
docs/TIMING_CALENDAR_DERIVATION.md.  The proven whole-word programs
remain the capability for the frozen shapes (byte parity), and the
minmax "macro-internal transient-copy event" below is resolved: it
derives from the delayed store's source-reachability constraint, not
from a source instruction.  The section below is kept as the WP8-era
rationale.

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
`row-live-through`, `cc-enable-unproved`.
Schedule: `event-delay-unproven`, `sequence-encoding-unproven`,
`template-capacity-exceeded`, `port-conflict`, `latency-violation`,
`target-macro-encoding-unproven` (table-absent CPU),
`cc-template-unproved` (WP9: an unprovable CC realization).
Descriptor: `descriptor-program-unproven`,
`descriptor-encoding-failed`, `descriptor-verification-failed`,
`cc-template-unproved` (WP9: a failed CC-model obligation).
Formation: `config-ownership-unproven`, `planned-lreg-live`,
`all-lanes-proof-missing`, `loop-preheader-unproven`,
`zero-trip-preheader-unproven`, `loop-body-not-owned`,
`unprofitable`, `stride-not-absorbed`.
Prefix epoch (WP11; refusing keeps the per-tile prefix, never blocks
formation): `prefix-epoch-invalidated`, `prefix-epoch-unproven`,
`prefix-hoist-preheader-unproven`.
Drain placement (WP13; refusing keeps the full derived drain, never
blocks formation): `drain-follower-opaque`, `drain-lreg-overlap`,
`drain-cc-live`, `drain-config-overlap`, `drain-dst-raw`,
`drain-delay-unproven`, `drain-horizon-spill`.
Residency (WP13; refusing keeps today's placement, never blocks
formation): `resid-skip-path-unproven` (function-wide owned-state
invariance walk failed; the underlying epoch name and insn are cited
in the dump line), `resid-span-unproven` (de-duplication span dirty),
`resid-dominance-unproven` (content matched but no dominating resident
program).  Reserved for the future eviction-choice increment, no
current emission site: `resid-capacity-exceeded` (with content-equality
de-duplication the descriptor register file is never contended, so
increment 1 has no honest site for it).

## 5. Flags

`-mtt-tensix-macro-planner` (formation), `-mtt-tensix-macro-planner-analyze`
(report-only), `-mtt-tensix-macro-planner-verify` (differential
verifier; forced under internal checking),
`-mtt-tensix-macro-planner-verify-corrupt-template` (verifier
self-test), `-mtt-tensix-macro-planner-replay` (WP10 delivery: admit
planner-formed launches into automatic replay recording; see Sec. 6),
`-mtt-tensix-optimize-drain-schedule` (WP13: per-boundary drain
placement proofs; see Sec. 2d).
`-mtt-tensix-macro-planner-residency` (WP13 delivery: descriptor-program
residency; see Sec. 2d).
All default off; default codegen is byte-identical to the
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
* WP9 carry-forwards: (a) the region-scoped ownership proof tolerates
  foreign code AFTER the region (the LLK own-descriptors convention) --
  an accepted risk of the same class as the M1 exit-block exemption;
  (b) RESOLVED at WP10: the first-row peel (one explicit row per loop
  trip) is superseded by the materialized preheader enable -- the
  peel's own proof source, emitted once under the rvtt_cc
  outermost-CC-depth contract (see the Formation bullet in Sec. 2a);
  (c) RESOLVED at WP10: the handwritten
  3-slot select calendar is now the derived COMPACT candidate (Sec. 2a)
  -- restore on the second carrier, explicit-load auto-increment stride
  absorption, launch-sourced store mod0 through the proven whole misc
  word 0x770; the real Int32/UInt32 TTNN Where kernel forms it
  end-to-end (the fp16b selector kept the 4-slot calendar because its
  condition and store modes differ; since the 2026-08-17 silicon
  adjudication was root-caused that calendar refuses the architectural
  `cc-restore-store-race` -- see Sec. 2a -- and matching the
  handwritten kernel's uniform-mode condition load is the remediation
  path for the fp16b rows, landed as the tt-metal condition-mode
  unification); (d) RESOLVED at WP10 behind the opt-in
  `-mtt-tensix-macro-planner-replay` flag: planner-formed SFPLOADMACRO
  launches are audited into the automatic replay model (a launch is a
  pure instruction word; recording captures the word, never state;
  execution at a replay site reads the then-current descriptor
  configuration -- the production handwritten Where kernel's own
  recorded launches are the architectural precedent), so the compact
  loop body's identical rows record once and replay: the real Int32
  TTNN Where face drops to 13 issued words (record + two recorded rows
  + three replays + drain), from 27 at WP10(c) and 39 at WP9.  The
  flag defaults OFF so every other planner-formed calendar (minmax,
  signbit, typecast -- whose repeated launch runs would also wrap)
  stays byte-identical until each is CRAQ-proven wrapped and the flag
  can default on; the 4-slot select rows keep their typed TTINCRWC
  separator, which remains an unaudited replay barrier -- auditing it
  is a follow-up with the same discipline.  A further increment would
  have the planner emit a record-only preheader capture plus per-trip
  replay launches itself (amortizing the record across trips and
  removing the RISC-paced execute-while-record occurrence, which is
  also the silicon-robustness concern for RISC-pushed CC calendars).

## 2c. WP12: generic multi-sub-unit integer rows (the MulInt32 class)

Lane BH, 2026-08-18 (`agent/mulint32-win`).  Target: the corpus' worst
open loss, mul_int32 at +98.16% (562.6 vs hand 283.9, MATH_ISOLATE) —
a 17-slot integer row (2 INT32 loads + sign-magnitude casts + 4
SFPMUL24 partial products + shifts/accumulates + store) against the
handwritten kernel's 4-template SFPLOADMACRO program at 8 issued
words/row.  Every increment is a capability fact or a dataflow proof;
none names an operation or a calendar.

1. **Generalized hosting (rvtt-macro-sched.cc).**  A value event is a
   hosting candidate when the SFPLOADMACRO override function realizes
   it exactly (SFPLOADMACRO.md functional model): its single written
   physical register is a carrier load's destination (the launch-VD
   chain — a scheduled template's result is always the launch VD or
   LReg16), or it is the store's sole data producer (dataflow-ordered
   consumer scan) and rides the store's carrier.  MAD-class events are
   admitted (the sub-unit legality table already carried SFPMUL24).
   One event per (carrier, realized sub-unit); overflow and
   probe-refused events stay explicit issues.  Events outside the
   single-result class (the dual-result binary swap) keep the
   established read-based rule for the proven whole-word programs.
   Hosting is capacity-aware: distinct probed template tuples are
   counted against the InstructionTemplate budget, and the store's
   sole producer is admitted first (it is load-bearing for the derived
   store-source realization).

2. **Generic derived template classes (rvtt-macro-desc.cc,
   `derived_value_template_fields`).**  Beyond the constant-register
   SFPSWAP family: SFPCAST (audited mods 0 / BH 3; in-place VC:=VD
   route or a name-encoded surviving VC), register-form SFPIADD
   (audited mods, accumulator must BE the launch VD so VB:=VD is the
   identity), the in-place immediate SFPSHFT realized as the SHFT2
   immediate template on Round (the frozen signbit pair, NOTES 9(e)),
   and BH SFPMUL24 (plain LOWER/UPPER mods; VB factor = launch VD; VC
   pinned to the architectural zero register L9 per SFPMUL24.md; VA in
   its own template field through the imm12 region).  An encoded VC of
   L0 is indistinguishable from the src-unused convention and refuses.

3. **Template sharing.**  Bit-identical derived field tuples encode to
   bit-identical template words and share one InstructionTemplate
   destination (event_spec.template_key; capacity counts distinct
   slots).  Ownership widened to the full LoadMacroConfig class
   {0..8} = 0x01ff (SFPCONFIG.md: InstructionTemplate[0..3],
   Sequence[0..3], Misc are one architectural state class; the
   production hand inits program all of them; widening only makes
   foreign-access ownership proofs stricter).  encode_template admits
   the architectural dest selectors 0xc..0xf (VD = 12 + i).

4. **Fixed launch VDs.**  When a surviving name-encoded read (an
   explicit issue's operand or a hosted template's surviving source
   field) consumes a value carrier's loaded register, the alternating
   {0,1} pair would move the value away from the name its consumers
   use: every value carrier then keeps its own physical load
   destination, under a derivation-core lifetime obligation (every
   VD-consuming event executes before the next row instance's launch
   rewrites the register).

5. **Explicit-issue hazard model (rvtt-macro-derive-core.h).**  Rows
   whose hosted events share registers with explicit members carry
   WAR floors (an event's write retires strictly after every earlier
   explicit reader), later-consumer/overwrite deadlines
   (exec + latency − 1 <= reader slot; exec <= overwriter slot;
   scheduled events retire before same-cycle issues, S1/S2), and
   same-cycle anti-dependence edges between events.  The fixpoint
   computes earliest-feasible cycles, so a violated deadline is a
   genuine infeasibility and refuses (sequence-derivation-hazard).

6. **Store-only sacrificial VD.**  The launch's VDLo field encodes VD
   0..3 (VDHi puns with the address LSB and stays 0 for even Dst
   addresses), so when every low register is row-live the established
   lowest-free rule has no encodable answer: a PROVEN-CLOBBERABLE
   internal temporary serves instead — first row access is a write
   (the next instance never reads the garbage), not a pinned carrier
   VD, and no launched name field reads it.  Store-carrier extraction
   is VD-agnostic (launch_vd = −2): no admitted class may claim a VD
   identity on a garbage register.

7. **Expectation-builder parity fix.**  `rvtt_macro_build_expectations`
   now mirrors synthesis' uniform-mode program filter and falls
   through to derived-calendar expectations when `derive_structure`
   does not apply — previously a mode-mismatched row could key a
   different program in the verifier than the one synthesis realized
   (caught by the Layer-7 launch-word comparison during WP12 bring-up;
   the verifier dump now prints got-vs-expected launch words).

**Result on the fresh mul_int semantic body** (tt-metal
`agent/mulint32-semantic`, tests/helpers/include/fresh_cpp_operations.h):
BH forms a 3-macro, 4-template, 6-event derived calendar at ii=12
issued words/row (from 18 explicit), verify: ok, CRAQ bit-exact on the
corrected sim (32489dda) at OFF and the full ON set, both fresh impls
and both production arms.  Pre-registered silicon expectation:
~430±30 TILE_LOOP cycles/tile ≈ −20..24% causal, +50..60% vs hand
283.9 (from +98%).

**Named residual (mechanism gap, not a tuning gap):** the row's ten
simple-unit operations compete for at most one Simple slot per macro
and the launch-VD chain realization is hostage to the register
allocator's post-RA names (only in-place chains host).  Reaching hand
parity (8 words/row) needs the placement half of iterative modulo
scheduling over the five sub-units — reservation-table placement with
backtracking on top of this derivation layer — per the literature
scan's Idea 5 (Rau, IMS; `~/sfpi-uplift/literature-scan-20260818/
IDEAS.md`), plus either planner-directed register renaming or extra
carrier formation (the hand kernel's re-load trick).  Design-doc
follow-up, not implemented here.  The template-sharing dedup of this
increment is the seed of Idea 6's dictionary-selection residency
(canonical derived words shared kernel-wide); noted as overlap for
that follow-up.

## 2d. Drain-aware boundary placement (WP13; Step A+B of the drain design)

Under default-off `-mtt-tensix-optimize-drain-schedule` the planner's
emission stops multiplying the derived drain with the region's run
structure.  Pre-registered design (Lane AU, 2026-08-18,
`~/sfpi-uplift/drain-study-20260818/DESIGN-drain-aware-scheduling.md`);
implementation `rvtt_macro_drain_boundary_elidable`
(rtl-rvtt-schedule.cc), consumed by `form_region` before any mutation.

**The problem.** `core_drain_slots` (rvtt-macro-sched-core.h) derives
the greatest event writeback distance past a run's last issue slot from
the descriptor's own SequenceBits delay fields -- exact static facts of
the derived timing calendars.  Emission pays that many SFPNOPs after
EVERY run, so a region split into R runs by an architectural boundary
instruction (the minmax face advance) executes the drain R times --
and the replay window then captures the tail, replaying it verbatim
with every launch (minmax: 3 NOPs x 4 faces = 12 slots/tile where the
architecture needs 3).

**The derivation.** Every fact is descriptor data or an adjudicated
retirement-semantics fact of the corrected simulator (craq-sim
9f324140, src/tensix.cpp:9820-9945, pinned to the BlackholeA0
SFPLOADMACRO functional spec; the store-predicate rule silicon-
adjudicated 2026-08-17):

* Launch-latched (safe to mutate while events are in flight): the store
  event's Dst row (read AT LAUNCH, :9848-9853, 9905), the store format
  (:9907), and the Dst layout its decode reads (:9913-9917).  A pure
  Dst/RWC counter write -- the run-separator class discovery already
  admits, typed or the audited raw `.ttinsn` decode -- therefore cannot
  disturb any in-flight event.
* Live-at-execution (mutation inside the horizon races): the lane
  predicate (:9908-9911), LReg contents staged events read or write
  (:9830-9833), the LoadMacroConfig words, and Dst rows an in-flight
  store writes.
* Horizon arithmetic: writeback slot = carrier issue slot + programmed
  delay (the identical model `core_drain_slots` uses), and at most one
  instruction issues per cycle, so a follower word's position in the
  issue stream lower-bounds its cycle distance from the boundary; any
  dynamic stall moves follower accesses later -- the safe direction,
  since every proof is "follower access strictly after the last pending
  writeback".

**The verdict, per intra-region boundary.** Elide the run's drain
exactly when (a) the inter-run stream is discovery-admitted pure-RWC
separators only, all launch-latched; slot credit is granted only to the
classes no later pass absorbs (the dst-autoincr `AIC_RWC_STEP` contract:
FACE-class typed advances -- word count from the machine-description
length, 4 bytes per word -- and audited raw SETRWC-class words, exactly
one word by the extraction contract; INC-class TTINCRWC is absorbable
and earns nothing); (b) every access of the next run is ordered after every
pending event by the decoded arithmetic — per-event delays decoded from
the descriptor's OWN sequence words through the established SequenceBits
format (case bits 2:0, event executes at issue + 1 + delay bits 5:3;
`decompose_sequence_word`, provenance
docs/TIMING_CALENDAR_DERIVATION.md 1-2), cross-checked against the
descriptor's drain_slots (a mismatch refuses).  Same-cycle ordering
follows the same established transactional model: a staged event
retiring at cycle X retires BEFORE the front-end instruction issuing at
X executes ("retire-before-issue"), so a front-end access (an explicit
word, or a launch word's own immediate load) at the last retirement
cycle is admitted at EQUALITY, while two staged events at one cycle
remain a race (the silicon-adjudicated cc-restore-store-race failure
mode) and launched follower events keep the strict inequality; and
(c) the enumerated follower words cover the whole horizon.  The
final run's drain is the region's exit contract -- a formed function or
loop body must never hand in-flight events to an invisible follower
stream -- and is never elided.  Refusals are named and keep the full
derived drain byte-identically.

**What it does not do (follow-ups, pre-registered in the design):**
partial gaps (emitting `max(0, drain - credit)` NOPs at a refusing
boundary), shadow-filling a kept gap with proven-neutral init traffic
(Step C, the M3 coupling), and re-pricing the MOP per-run drain term
(design Sec. 7).  The profitability model (`run_profitable_p`,
`loop_profitable_p`) still prices the full per-run drain: pricing
follows placement conservatively, never ahead of it.
## 2d. WP13: descriptor-program residency (dictionary selection)

Adaptation of the dictionary-selection compression line (Lefurgy et
al., MICRO-30 1997; literature scan 2026-08-18 Idea 6): the
SFPLOADMACRO descriptor registers are a programmable dictionary, and
the planner's derived descriptor words are its entries.  WP13 selects
which entries stay RESIDENT kernel-wide so identical descriptor
programs are pushed once per kernel instead of once per region or per
enclosing-loop trip.  Default off (`-mtt-tensix-macro-planner-residency`);
every refusal keeps today's placement byte-identically.

Two increments, both implemented in `rvtt-macro-desc.cc` (solver) with
the emission wiring at the planner's WP11 call site and
`emit_planner_run`:

* **Outward span extension** (`rvtt_macro_residency_extend`).  After a
  successful WP11 hoist, the per-level configuration-epoch proof
  (`rvtt_macro_prefix_epoch_hoist`, full conservative discipline
  including the foreign-LREG/CC refusal) is iterated through
  successively enclosing loops; the final proven level's structural
  preheader (or commit-time entry-edge split) receives the hoisted
  block.  The occurrence obligation the multi-level hoist adds -- the
  resident program may now execute on paths that never reach the
  region -- is discharged by the function-wide OWNED-STATE INVARIANCE
  walk (`rvtt_macro_epoch_owned_state_invariant_p`): outside the
  planner's own emissions and the region, no instruction of the
  function may write an owned SFPCONFIG destination or deliver an
  unresolvable/opaque word, so executing the programming words early is
  observationally inert (the copied enable re-asserts the outermost-CC
  all-lanes contract under WP11's materialization license).  Placement
  executions are monotone non-increasing on every loop path, so no
  benefit threshold applies; the walk deliberately ADMITS foreign
  LREG/CC dataflow and owned-destination reads (a placement move
  cannot be observed through them -- rationale at `resid_insn_check`).

* **Content-equality de-duplication** (`rvtt_macro_residency_lookup`).
  Descriptor programs are canonicalized by CONTENT: the bit-exact
  derived template/sequence/misc words (SETC16 programs and launch
  tuples excluded -- they are per-region regardless).  A later formed
  region whose canonical words equal an already-programmed entry, whose
  launch block the entry's placement dominates (regions are processed
  in forward program order; the increment-1 first-formed-wins policy),
  and whose function-wide owned-state invariance holds, elides its
  descriptor-word programming entirely; its retained ambient enable and
  owned SETC16 program stay.  Eliding a bit-identical rewrite is
  observationally inert given the values provably reach the launches.
  Different canonical content simply programs per-region as today (no
  eviction, no contention: identical content shares destinations by
  construction).  The selection value function is the R2 delivery
  model (rvtt-cost.md, RISC_PUSH_X100 = 123 centislots per pushed
  word), used for dump diagnostics and ordering only -- never for
  shape keying; capacities and field layouts are capability-table
  facts.

The per-region ambient enable and the owned SETC16 program are never
elided or moved by WP13: their contract discharges (AT PREFIX-LEDGER
rows 1-4 -- the CC epoch through the per-tile LaneConfig reset, and
the SETC16-visible state reachable from data-plane MMIO) are separate
named follow-ups.  Replay-record residency (the replay-linker half of
the same dictionary idea: keeping a RECORDED RANGE resident across
tiles) is NOT implemented here: on the measured where shape the
per-trip re-record is execution-covered (Lane-S saturation -- removing
pushes below the execution floor realizes ~0), the hand kernel pays
the same re-record class, and the emission home is the replay pass;
recorded as a design note with the residency solver as its intended
slot allocator when a push-bound shape appears.
