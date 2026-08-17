# Timing-calendar derivation: sequence words and delay bits from the row DAG

Lane R1 design document (2026-08-16 overnight).  Target frontier: the
named refusals `sequence-encoding-unproven`, `event-delay-unproven`,
`descriptor-program-unproven` that block macro FORMATION on rows the
planner now discovers cleanly (unary max/min swap rows, add/sub_int
7-slot rows, and ultimately the MulInt32 delayed-template class).
This document establishes that the sequence-word bit format and the
per-event delay semantics — carried until now as "architecturally
UNESTABLISHED" whole-word capabilities (docs/MACRO_PLANNER.md §3,
NOTES-wp6-prep.md §9(b)/(c)/(g)) — are in fact fully established by
three mutually independent sources, derives every frozen calendar word
from its schedule as validation, and specifies the generic derivation
layer that replaces frozen per-shape calendars for new shapes.

Nothing here relaxes the standing rule: raw words live only in the
capability/encoding tables and test expectations; refusals are named
and never mutate; genericity is proven by renamed-equivalent, varied-
constant, and near-miss tests.

## 1. The three architectural sources

**(S1) The ISA functional specification** (`SFPLOADMACRO.md`,
BlackholeA0 ISA documentation; local copy
`craq-sim/tests/aristotle/mega-union/specs/SFPLOADMACRO.md`).  Gives,
normatively: the SequenceBits per-byte format, the per-sub-unit opcode
legality table, the Simple/Round same-cycle rule, the SFPSWAP
scheduling constraint, the store field-override rules, the
`Misc` field layout including `UnitDelayKind`, the delay-counting
semantics, and the issued-instruction discard rule.

**(S2) The CRAQ simulator's generic executor** (craq-sim @ f80a8d64,
`src/sfploadmacro_events.h` + `src/tensix.cpp`
`TENSIX_EXECUTE_SFPLOADMACRO` / `tensix_retire_load_macro_events`).
The executable model the correctness gates run against: absolute ready
cycle = issue + 1 + Delay; per-sub-unit one-event-per-cycle FIFOs with
earliest-ready/oldest-first arbitration; retirement before same-cycle
issue; same-cycle groups evaluated transactionally from one snapshot
with disjoint-write merging; SWAP's extra MAD resource claim; the
launch-latched store context (Dst row, format, layout -- since
craq-sim 9f324140 NOT the lane predicate, which is the LIVE CC at
store execution, silicon-adjudicated 2026-08-17); the deferred-CC
one-cycle visibility latch.

**(S3) The production handwritten MulInt32 LLK**
(tt-metal `tt_llk_blackhole/common/inc/sfpu/ckernel_sfpu_mul_int.h`,
`_init_mul_int_`), which programs its descriptor through the same bit
vocabulary with author-annotated field meanings (`UnitDelayKind`,
`UsesLoadMod0ForStore`), independently confirming the byte packing,
the 0xC template dest selector, the store byte convention, and misc
word 0x330.

These sources agree with each other and with every frozen calendar
word (§3).  §9(b)/(c)/(g) of NOTES-wp6-prep.md are therefore RESOLVED:
the bit-level sequence format is established, and per-event delays are
derivable.

## 2. The established facts (new capability-table content)

All of the following become per-CPU capability-table data with the
provenance labels (S1)/(S2)/(S3).  WH and BH are common for all of it
(the existing descriptor-state words are WH/BH-common by test
evidence; S1 is the BlackholeA0 spec and S2 implements both).

### 2.1 Sequence word format (S1, S2)

`Sequence[macro]` is four bytes, byte *i* programs sub-unit *i*:
0 = Simple, 1 = MAD, 2 = Round, 3 = Store.

Per byte:

| bits | field | semantics |
|---|---|---|
| 2:0 | case | 0 skip; 1 undefined; 2 NOP; 3 SFPSTORE; 4+k template k |
| 5:3 | delay | event executes at issue + 1 + delay (see 2.4) |
| 6 | VD16 | event's VD := LReg[16] (compute: write; store: read source) |
| 7 | route | sub-units 0-2: Insn.VB := launch VD (and VC := template VD if the op has no VC); clear: Insn.VC := launch VD (and VB := template VD if the op has no VB).  Store: 3-way (bit6 → L16; else bit7 → leave template VD (case-3 store: L0); else launch VD) |

Special S1 rules folded into the format facts:

* SFPSHFT2 in SHFT_IMM mode with route=1 (VB := launch VD) skips the
  usual `VB = Imm12 & 15` aliasing — the immediate-shift template can
  read the launch VD without the imm12 low nibble interfering.
* An event with `delay != 7` forgets any pre-existing scheduled
  instruction at the same execution time (S1).  The planner never
  relies on either behavior: derived schedules prove no same-unit
  same-cycle convergence instead.

### 2.2 Sub-unit legality (S1, normative table)

| sub-unit | opcodes |
|---|---|
| Simple | SFPABS SFPAND SFPCAST SFPCOMPC SFPCONFIG SFPDIVP2 SFPENCC SFPEXEXP SFPEXMAN SFPIADD SFPLZ SFPMOV SFPNOP SFPNOT SFPOR SFPPOPC SFPPUSHC SFPSETCC SFPSETEXP SFPSETMAN SFPSETSGN SFPSHFT SFPSWAP(‡) SFPTRANSP SFPXOR |
| MAD | SFPADD SFPADDI SFPLUT SFPLUTFP32 SFPMAD SFPMUL SFPMULI SFPNOP (+ SFPMUL24 on BH, by S3) |
| Round | SFPNOP SFPSHFT2 SFPSTOCHRND |
| Store | SFPSTORE |

An opcode scheduled on a sub-unit that cannot execute it becomes
SFPNOP silently (S1) — i.e. wrong placement silently corrupts data.
Placement is therefore a hard architectural fact, not a heuristic:
the table is transcribed per CPU, keyed by opcode byte, and any event
whose opcode has no entry for a free sub-unit refuses by name.

This resolves the puzzle of the frozen calendars: the shift/copy
events run on the ROUND sub-unit (signbit, minmax) because SFPSHFT2 is
Round-only; the effects-layer `subunit` attribute classifies the
SOURCE instruction, while placement follows the TEMPLATE opcode that
realizes it.

### 2.3 Same-cycle and adjacency constraints (S1 (†)/(‡), S2 resources)

* (†) A Simple event and a Round event executing in the same cycle
  must split the VD16 bit: one writes LReg[16], the other does not.
* (‡) SFPSWAP scheduled on Simple executing at cycle E requires: MAD
  hosts nothing at E (S2 models this as SWAP claiming the MAD
  comparator), and Simple and Round host nothing at E + 1.
* Each sub-unit executes at most one event per cycle, globally across
  all in-flight launches (steady-state: modulo the row initiation
  interval).
* Same-cycle events must have disjoint architectural write sets (S2
  rejects conflicting writes; S1 gives no ordering).
* An explicitly issued SFPU instruction whose sub-unit receives a
  scheduled event the same cycle is silently DISCARDED (S1).  Derived
  schedules must therefore keep explicit issues' sub-units clear of
  scheduled arrivals in their issue cycle.

### 2.4 Delay counting (S1, S3) — and the honest boundary

The 3-bit delay counts down from issue; the event executes the cycle
after the counter reaches zero (delay 0 = the cycle immediately after
the launch; matches S2's issue+1+delay).  `Misc` bits 11:8
(`UnitDelayKind`, one bit per sub-unit) select what decrements the
counter: 0 = every cycle, 1 = every instruction the thread issues to
the SFPU.  The semantics are MODAL: if ANY outstanding scheduled
instruction is instruction-counted, ALL outstanding counters decrement
per issued instruction (S1).

Instruction counting exists for events whose input is produced by an
instruction ISSUED AFTER their launch (S1 says exactly this; S3's
MulInt32 relies on it: the MAD template consumes the explicit `load b`
issued after the SFPLOADMACRO).  Under a front-end stall a
cycle-counted event would execute before its forward-issued producer.

The CRAQ simulator ignores bits 11:8 and paces the front end to one
SFPU instruction per cycle, under which the two counting models
coincide for SFPU-dense streams.  Silicon distinguishes them only
under stalls.  Derivation rule (conservative, dual-model):

* For every event, the timing proof must hold under BOTH counting
  models given the formed stream.
* If an event has a forward-issued input dependence, its sub-unit's
  kind bit is set to instruction-counting AND every issue slot in the
  descriptor's live window must be an SFPU-class instruction
  (SFPLOAD/SFPLOADI/SFPLOADMACRO/SFPNOP/compute/store — the formed
  calendar controls this; drains already use SFPNOP).  A window
  containing a non-SFPU issue (e.g. an unabsorbed TTINCRWC separator)
  refuses `delay-model-unproven`.  (Open architectural question,
  documented: whether TTINCRWC decrements the instruction counter.
  The silicon-proven signbit calendar sets Simple=instruction-counted
  with an SFPU-sparse window and is correct on silicon, which suggests
  separators do count; until that is established independently, the
  derivation does not rely on it.)
* Otherwise the kind bit derives to cycle-counting, which is
  stall-robust for events whose inputs are all latched at or before
  their own launch.

### 2.5 Misc word (S1, S3)

`Misc` = `UnitDelayKind[3:0] << 8 | UsesLoadMod0ForStore[3:0] << 4 |
StoreMod0[3:0]` (bits above 11 unused; S2 masks to 0xFFF).

* `StoreMod0`: the delayed store's data format when the store macro's
  `UsesLoadMod0ForStore` bit is clear.
* `UsesLoadMod0ForStore` bit k: macro k's store takes the LAUNCH
  word's Mod0 instead.  Derivation: set exactly the store-carrying
  macro's bit when the store's data mode equals that carrier's launch
  mode (true by construction for a merged in-place carrier and for a
  store-only carrier, whose launch encodes the store's own access);
  otherwise clear it and derive StoreMod0 from the store's typed mode.
* `UnitDelayKind`: per 2.4.

The frozen words set additional bits in dead positions (see §3.4);
those are conventions of their authors, functionally inert in both S1
and S2 for the calendars they accompany, and are NOT reproduced by
derivation.  Frozen shapes keep their frozen words (§4.1), so byte
parity is unaffected.

### 2.6 Value visibility (S2, already partially in the tables)

* The launch's SFPLOAD executes at the launch's own issue slot; a
  scheduled event executing at cycle >= slot + 1 reads it.
* Events retire before same-cycle issues: an event executing at cycle
  E reads LReg state before any instruction ISSUED at E writes it.
* Producer event -> consumer event needs exec(consumer) >=
  exec(producer) + 1 (same-cycle groups read one pre-write snapshot).
* CC visibility lag = 1 is a table fact (`cc_visibility_lag`).  The
  store's LANE PREDICATE is the LIVE CC at the store's execution cycle
  (`store_lane_mask_live_at_execution`; a same-cycle CC retire is not
  yet visible, an earlier-cycle one is): the ISA's launch-latched store
  overrides are exhaustively Addr, the Mod0 source, and the backdoor
  bit (SFPLOADMACRO.md StoreSubUnit extras).  Established by the
  2026-08-17 BH silicon adjudication
  (~/sfpi-uplift/where-adjudication-20260817) and the corrected CRAQ
  executor (craq-sim 9f324140); it supersedes the earlier S2-only
  launch-latched reading (`store_lane_mask_latched_at_launch`,
  retired).  Consequence for CC calendars: the all-lanes restore must
  retire STRICTLY BEFORE the scheduled store executes
  (`cc-restore-store-race`); the 4-slot select calendar violates this
  (restore exec 3 == store exec 3), the compact 3-slot calendar
  satisfies it (2 < 3).
* LReg[16] is a macro-only staging register: writable by compute
  events (VD16), readable ONLY by the delayed store (VD16).  Compute
  templates cannot read it (no encodable index).

## 3. Paper validation: every frozen word re-derived

The seq_program table's structural event lists were reverse-engineered
without the bit format; the actual bytes tell the true story.  Decode
notation: `unit: case/delay/VD16/route`.

### 3.1 minmax-binary (m0 = 0x00dd008c, m1 = 0x53000000)

Schedule (the planner's, unchanged): slot0 launch m0 (load a, VD
alternating 0/1), slot1 explicit load b (L1), slot2 launch m1
(store-only, VD3, absorbs stride); ii = 3.

* m0 Simple byte 0x8c = T0(SFPSWAP)/delay1/VD-/route1.
  Delay DERIVES: SWAP consumes the explicit load b issued at slot1;
  events retire before same-slot issues, so earliest exec = slot2 =
  0+1+1.  route=1 (VB := launch VD) SHIELDS the template's VC (= L2,
  the planned second operand) from the VC-override; SFPSWAP reads
  VD/VC, so route=1 is exactly "the launch VD does not enter through
  VC".
* m0 Round byte 0xdd = T1(SFPSHFT2 imm-0 copy)/delay3/VD16/route1.
  This is the "macro-internal transient-copy event with no derivable
  source instruction" (MACRO_PLANNER.md §3).  It IS derivable — not
  from a source insn but from the store-source reachability fact: the
  delayed store can read only {its own launch VD, LReg16}; the row's
  final value sits in macro 0's VD, unreachable from macro 1's store;
  therefore a STAGING COPY (SFPSHFT2 imm 0, the only Round-legal move,
  route=1 reading the launch VD by the SHFT_IMM special rule, VD16
  writing LReg16) is inserted.  Delay derives: >= SWAP+1 = 3 by
  dependence, and >= 4 by (‡) (Round must be idle the cycle after
  SWAP's exec at 2) — so exec 4, delay 3.  EXACT.
* m1 Store byte 0x53 = store/delay2/VD16(read L16)/route0.
  Derives: exec >= copy + 1 = 5 = slot2 + 1 + 2 -> delay 2.  EXACT.
* (‡) MAD idle at SWAP's cycle: nothing on MAD.  Steady state ii=3:
  Simple at slots ≡2, Round ≡1, Store ≡2 (mod 3); store_N and
  SWAP_{N+1} share a cycle on different units with disjoint writes.
  VD alternation derives from the copy reading VD at slot+4 after the
  next row's launch would have overwritten a shared VD.
* Structural correction to the table: the copy belongs to MACRO 0's
  word (Round byte of 0x00dd008c); the frozen seq_program table
  records it under "minmax-binary-m1", which never mattered because
  the word was carried whole.  The derived model keys events by
  (carrier, sub-unit) truthfully.

### 3.2 signbit (m0 = 0x5384004d)

Schedule: one launch per row (load, VD fixed 1), separator kept; ii=2.
Row: shift(-31) -> cast -> store.

* Round byte 0x84 = T0(SFPSHFT2 SHFT_IMM)/delay0/VD-/route1: the
  shift is Round-ONLY (2.2), earliest exec = slot+1, route=1 shifts
  the launch VD (imm12 alias skipped), writes VD.
* Simple byte 0x4d = T1(SFPCAST)/delay1/VD16/route0: cast is
  Simple-only; consumes the shift result via VC := launch VD
  (route=0); exec = shift+1 = slot+2; VD16 stages for the store.
  The cast reads VD in the same cycle the NEXT launch's load
  overwrites it — legal by retire-before-issue; this is why fixed VD
  works here.
* Store byte 0x53 = store/delay2/VD16: exec = cast+1 = slot+3.  EXACT.
* Note: the frozen table's transcription "store delay 3" was the
  documented "three issue slots after launch" (= 1 + field), not the
  field value; the field is 2.  The whole-word lookup hid this.

### 3.3 cast-round (m0 = 0x534d0004) and the select class

* cast-round: Simple 0x04 = T0(SFPCAST)/d0/VD/route0 (VC := launch
  VD); Round 0x4d = T1(SFPSTOCHRND)/d1/VD16/route0 (VC := launch VD =
  the cast's result); Store 0x53 = d2 from L16.  Chain +1/+2/+3 =
  earliest-feasible under forced placement.  EXACT, including the one
  fully documented delay triple ("Simple d0, Round d1, Store d2").
* select m0 = 0x13000004: Simple 0x04 = T0(SFPSETCC)/d0 (reads the
  condition launch VD at +1, earliest); Store 0x13 = d2/read-VD
  (bit6=0: the coalesced merge value lives in the shared launch VD;
  exec +3 = one past the last payload write at slot 2).  EXACT, and
  the WP9 CC model's slots re-derive unchanged.
* select m1/m2 = 0x00000005: Simple 0x05 = T1(SFPENCC)/d0: restore
  executes at its carrier slot + 1, visible +1 later (cc lag), by the
  next row's first slot (the kept separator).  EXACT; confirms the
  word does not encode the macro index (the WP9 finding).

### 3.4 Misc words

| shape | frozen | derived | divergence |
|---|---|---|---|
| minmax | 0x330 | 0x120 | frozen bit4 (UsesLoadMod0 for macro 0, which has no store) and bit9 (MAD kind, MAD hosts nothing) are dead by S1+S2; S3 shows the 0x330 convention covers the author's TWO calendar variants (store on macro 0 or 1) with one word.  Live bits agree: bit5 (in-place store takes launch mod0), bit8 (Simple instruction-counted, REQUIRED by SWAP's forward-issued load-b dependence, window all-SFPU). |
| signbit | 0x110 | 0x010 | live bit4 agrees; frozen bit8 (Simple instr-counted) is not required by the derivation (the cast's producer is same-macro) and its window contains the non-SFPU separator (2.4 note). |
| cast-round | 0x100 | 0x000 | live bits (StoreMod0 = 0 via bits 3:0, no launch-mod0) agree; frozen bit8 as above. |
| select | 0x706 | 0x306 | live bits (StoreMod0 6, no launch-mod0 — the definition carrier's mode is free) agree; frozen bits 10:8 = 0x7 vs derived 0x3?  Derived: Store's data is written by forward-ISSUED payload loads -> the store unit's kind bit (bit11) would be required, but the frozen word leaves bit11 CLEAR and sets 10:8 — the production select protocol relies on dense issue rather than kind bits.  Kind-nibble derivation is therefore documented as stall-robustness policy, not reproduction. |

Conclusion: every SEQUENCE word reproduces bit-exactly from the
schedule plus the §2 facts.  Misc words reproduce in every live bit;
dead-bit and kind-nibble conventions differ and are documented.  The
frozen shapes keep their frozen words (§4.1) so all byte-parity
oracles hold trivially; the reproduction proof lives as standalone
unit tests over the derivation core (§6).

## 4. The derivation layer

### 4.1 Position in the pipeline

Descriptor synthesis order becomes:

1. `find_program` over the proven `desc_programs` (unchanged) — the
   frozen shapes keep byte-identical emission and their envelope pins.
   Program matching gains an operand-layout probe: a program whose
   template rules reference source operands the admitted insn does not
   have (e.g. the constant-register SFPSWAP variants vs the
   `binary-periodic` swap_int layout) does not match, instead of
   matching and refusing `descriptor-encoding-failed`.
2. If no proven program matches: DERIVED synthesis (new,
   `rvtt-macro-derive.{h,cc}` + freestanding core
   `rvtt-macro-derive-core.h` following the sched-core/verify-core
   pattern).  Any failed obligation refuses by name and falls back to
   the next carrier-grouping candidate exactly as today.

### 4.2 Inputs

From the schedule (unchanged vocabulary): per-carrier issue slots,
hosted value events (in program order) and the delayed store, explicit
issues and their slots, the row interval, VD policy, absorbed stride.
From the effects/tables: each event's TEMPLATE opcode byte (the
realizing instruction — the source's own opcode, or a table-designated
realization such as SFPSHFT2 for an explicit SFPSHFT), LReg def-use
edges among row members, and each event's launch-VD consumption
position (does the launch value enter through VC, through the VD/VB
side, or not at all).

### 4.3 Algorithm (deterministic, refusing)

1. **Placement.**  Each hosted value event goes to the sub-unit its
   template opcode is legal on (2.2).  The legality sets are nearly
   singletons; where an opcode admits multiple units the first legal
   free byte in unit order Simple, MAD, Round is taken.  A carrier
   whose events collide on one byte refuses
   `subunit-placement-unproven` (so the deterministic candidate search
   proceeds — e.g. add_int's four Simple-class events on two carriers
   refuse both existing candidates honestly, §5.2).
2. **Staging copies.**  If the store's data value lives neither in its
   own carrier's VD nor in LReg16 at store time, insert the staging
   copy (2.6, 3.1): SFPSHFT2 imm-0 route-VB VD16 on the producing
   carrier's Round byte (a table-designated realization with
   provenance = the frozen minmax copy).  If that byte is occupied or
   the producing value is not the producer carrier's launch VD (the
   copy can only read its own launch VD), refuse
   `store-source-unreachable`.
3. **Delays.**  Earliest-feasible, iterated to fixpoint over: (a)
   dependence edges (2.6: producer + 1; forward-issued producers:
   their issue slot + 1); (b) sub-unit occupancy, one per cycle
   globally, steady-state modulo ii; (c) (†) VD16 disjointness for
   same-cycle Simple/Round pairs; (d) (‡) for every SFPSWAP; (e)
   same-cycle write-set disjointness; (f) explicit issues' sub-units
   clear of scheduled arrivals (2.3); (g) store exec after every write
   to its source, and its source's next overwrite after the store.
   Any delay > 7 refuses `delay-range-exceeded`; an unsatisfiable
   constraint refuses `sequence-derivation-hazard`.
4. **Bits.**  VD16 from the planned target (LReg16 staging or final
   store staging); route from the launch-VD consumption position (VC
   consumer -> 0, otherwise 1, which also shields a planned physical
   VC); store byte source per the 3-way rule restricted to the PROVEN
   kinds {own launch VD, LReg16} (the leave-template-VD store form is
   unproven and refuses).
5. **Words.**  Sequence bytes packed by a pure field packer in the
   tables (`encode_sequence_derived`); misc packed per 2.5; templates
   through the existing `encode_template` with fields from the
   admitted source operands (operand layout keyed by unspec, the
   sanctioned encodability key) or from table-designated realizations
   (staging copy).  Delay-kind bits per 2.4 with the SFPU-dense-window
   obligation, else `delay-model-unproven`.
6. **Drain** = greatest remaining exec distance past the last issue
   slot (the existing generic rule, now with real per-event delays).

### 4.4 Verification

The Layer-7 verifier re-derives the full calendar through the shared
derive-core from the region's explicit facts and compares words
field-by-field, then re-checks all §4.3 inequalities from the
exchanged slots (as the WP9 CC model already does).  The
same-table-as-synthesis limitation carries over and remains covered by
the frozen-oracle byte-parity suite plus the §6 reproduction tests,
which pin the derive-core against independently recorded words.

### 4.5 Refusal vocabulary (append-only additions)

`subunit-placement-unproven`, `store-source-unreachable`,
`sequence-derivation-hazard`, `delay-range-exceeded`,
`delay-model-unproven`.

## 5. Derived calendars for the blocked shapes

### 5.1 unary max/min (this increment's formation target)

Row: load, SFPSWAP-with-constant-register, store (in place, stride 2);
discovered 8-row loop region.  Candidate 0 (merged, ii=1): SWAP_N
executes at N+1, SWAP_{N+1} at N+2 — violates (‡) Simple-idle-next
-> `sequence-derivation-hazard`.  Candidate 1 (store demoted, ii=2):

* macro 0 (load carrier, alternating VD): Simple byte
  T0(SFPSWAP-cst)/delay0/route1 = 0x84; Round byte staging
  copy T1/delay2/VD16/route1 = 0xd5 (dependence gives >= 2; (‡) is
  satisfied at delay 2 since the next SWAP executes at +3, not +2).
  Word 0x00d50084.
* macro 1 (store-only carrier, planned sacrificial VD, absorbs the
  stride): Store byte d2/VD16 -> word 0x53000000 — the identical store
  program as frozen minmax m1, now derived.
* Templates: T0 packed from the admitted swap-cst source (opcode byte
  via UNSPECV_SFPSWAP, VC = the constant register's architectural
  index, mod1 = the source's mod operand, dest selector 0xC); no
  hidden-write claim (SFPSWAP drops writes to VC >= 8, already the
  audited effects envelope).  T1 = the staging copy (table
  realization, dest selector 0xD).
* Misc: store mode == carrier launch mode -> UsesLoadMod0 bit 5; no
  forward-issued deps -> kind nibble 0 -> 0x020.
* (‡) checks: MAD idle always; Simple/Round idle at SWAP+1 (copy at
  +3).  Same-cycle SWAP_{N+1}/copy_N at +3 satisfies (†) (copy is the
  VD16 side).  The simulator's conservative SWAP write-claim may defer
  one of them a cycle; the dependence proof holds under deferral
  (deferral only delays, and the store's margin is proven from
  programmed times).

### 5.2 add/sub_int (7-slot row) — needs one scheduler increment

Row: load a, cast, load b, cast, iadd, cast, store — four Simple-only
value events (2.2).  Both existing carrier-grouping candidates host
all four on <= 3 carriers, so the derived path refuses
`subunit-placement-unproven` on both: honest, and strictly better than
today's opaque `sequence-encoding-unproven`.  Formation additionally
needs hosting-capacity awareness in the scheduler (Layer 3, R3
territory / next increment): host at most one Simple event per
carrier, leave the residue as explicit issues placed off the scheduled
Simple cycles (2.3 discard rule), and plan the final cast's
destination register to the store carrier's VD.  The derived paper
calendar at ii=5 (launch a + c1 hosted d0; launch b + iadd hosted d1;
explicit cast b; SFPNOP filler; explicit cast r; store on carrier a,
d4) satisfies every §2 fact and saves two issue slots per row before
drain amortization; the derivation layer specified here validates and
encodes it unchanged once the scheduler supplies that shape.

### 5.3 MulInt32 class (the +98% target)

The semantic row (load, load, mul24, store) derives directly: MUL24 is
MAD-legal (S3), hosted on the store-demoted calendar exactly as S3's
handwritten variants; the forward-issued load-b dependence sets the
MAD kind bit with an all-SFPU window.  The derived words for the
three-address variant equal S3's macro-1 program (MAD byte 0xCC =
T0/d1/VD16/route1, store 0x5B = d3/L16) — a second executable
reproduction target for §6.

## 6. Gates and tests

1. **Reproduction unit tests** (standalone, host-compiled like
   rvtt-macro-tables-test.cc): feed the frozen calendars' schedules
   into the derive-core; assert bit-exact sequence words (3.1-3.3),
   the misc live-bit agreements and documented divergences (3.4), and
   the S3 MulInt32 words (5.3).  These are the executable form of the
   paper validation.
2. **Byte-parity oracles**: unchanged (frozen shapes still resolve
   through the proven-program table first).
3. **Corpus flags-off identity**: derived path is reachable only under
   the planner flags.
4. **DejaGnu**: unary max/min formation byte-pinned on BH and WH;
   renamed-equivalent kernel; varied constants (different constant
   register, both swap senses); near-misses (candidate-0 (‡) refusal
   name; an unplaceable-opcode row refusing
   `subunit-placement-unproven`; delay-overflow refusing
   `delay-range-exceeded`); full tt suite FAIL set frozen.
5. **CRAQ**: unary max/min fresh_cpp nodes, BH+WH, OFF/ON, base
   (007a9c42294) vs edit compiler — ON forms and must be bit-exact
   with OFF through the generic descriptor-driven simulator path.
6. **No silicon** this increment (delay-kind bits are sim-dead; the
   dual-model obligations of 2.4 are the review surface before any
   silicon claim).

## 6a. Implementation status (this branch)

Landed (commits after the design):

* Capability-table facts (§2) in `rvtt-macro-tables.{h,cc}` with
  S1/S2/S3 provenance; the freestanding derivation core
  `rvtt-macro-derive-core.h` implementing §4.3; the standalone
  reproduction suite `rvtt-macro-derive-test.cc` = the executable form
  of §3 (77 checks green on BH and WH: all frozen sequence words
  bit-exact, the handwritten MulInt32 three-address words bit-exact
  including the MAD result-latency-2 store delay, the one-slot
  lifetime refusal, the add_int placement refusal, the derived unary
  max/min calendar).
* Descriptor synthesis (`rvtt-macro-desc.cc`) tries the derivation
  when no proven whole-word program matches; proven-program matching
  gained the operand-EXISTENCE probe (a layout variant is not the
  proven program; a present-but-non-constant operand keeps the
  established encodability refusal).  The verifier's expectation
  builder re-runs the shared derivation.
* The admitted derived template class starts at the constant-register
  SFPSWAP family: the unary max/min LOOP shape forms on BH and WH
  (both senses derived from the operand layout; byte-pinned +
  renamed/varied tests), the straight-line runs refuse
  `unprofitable`, the sub-vector mod near-miss refuses
  `descriptor-program-unproven`, and the merged ii=1 candidate
  refuses `sequence-derivation-hazard` ((‡)), driving the
  deterministic candidate search to the formable demoted schedule.
* rvtt.exp: 2278 passes; the FAIL set is the pre-existing
  environmental 15 (zaamo-18789, delay-34602, sfpxloadi-bh,
  unused-46063, 41863-consteval).

**WH boundary (found by the CRAQ gate, 2026-08-17).**  The BH derived
unary max/min formation is CRAQ bit-exact end to end (real kernel,
generic simulator path, rc=0 all four BH cells).  The SAME calendar on
the WH simulator returns position-shuffled tiles after the first,
while the launch trace shows every latched Dst row, lane mask, and
retirement cycle correct — and a control run with a refusing compiler
passes, isolating the failure to the WH-formed calendar.  WH select
(WP9), which absorbs nothing, passes this path; the discriminating
ingredient is the ABSORBED-STRIDE launch auto-increment through the
WH dual-slot address-modifier machinery — the already-open WH
Dst-advance frontier (FINDING-wh-dst-autoincr-fresh-maxmin.md, WP8
§6b(4)).  The derived path therefore carries a per-CPU envelope fact
(`derived_stride_absorption_proven`: BH yes, WH no) and WH
absorbed-stride derivations refuse `derived-stride-absorption-
unproven` byte-identically (pinned by the WH twin test).  Note the
frozen minmax/signbit calendars absorb strides on WH through the
proven-program path and have never been executed on the WH simulator
against a real kernel — the same latent frontier, pre-existing and
now precisely named.

**WH boundary DISCHARGED (2026-08-17, lane AP).**  The WH
dst-autoincr adjudication (sfpi-gcc 2a0ba1e6602;
laneAJ-evidence-20260817) convicted the compiler's DUAL-SLOT SETC16
program, not the absorption machinery: the base-0 bank words (SETC16
11/25/50) clobbered LLK's live ADDR_MOD_2, corrupting the NEXT tile's
base-0 FPU datacopy — exactly the position-shuffled-tiles signature
above (SFPU stores were always correct, which is why the launch trace
was clean).  The capability tables now carry the corrected
single-slot Base=1 program ({19,29,54}, physical slot 6 under the
LLK-pinned ADDR_MOD_SET_Base=1), `derived_stride_absorption_proven`
is BH+WH, and the WH derived unary max/min calendar forms and is
CRAQ bit-exact multi-tile on the corrected WH simulator (wh
8f0079a9; laneAP evidence).  The WH twin test now pins the FORMATION;
the stride-4 near-miss pins the remaining +2-only envelope
(`stride-not-absorbed`).

Additional finding while validating §3: the handwritten MulInt32
ONE-SLOT in-place variant (`dst_index_in0 == in1 == out`) fails the
derived LReg16 lifetime proof — the next row's MUL24 rewrites LReg16
strictly before this row's store executes under both counting models
at a one-slot initiation interval.  The two- and three-slot variants
sit exactly on (and inside) the snapshot boundary and are sound.
Worth an upstream question to the kernel's author; the derivation
refuses the one-slot shape.

## 7. Carried risks / open questions

* Whether TTINCRWC (and other non-SFPU Tensix issues) decrement
  instruction-counted delays (2.4).  Sim-invisible; constrains only
  shapes needing instruction counting with sparse windows, which
  refuse until established.
* The simulator does not model S1's same-cycle discard of explicit
  issues, (‡), or (†); derived schedules PROVE these statically so
  CRAQ-passing calendars do not rely on unmodeled behavior, but a
  sim-side check (assert on discard/adjacency violations) would close
  the loop — sim territory, not this lane.
* Misc kind-nibble policy reproduces no frozen word bit-exactly by
  design (3.4); if a future silicon experiment shows kind bits matter
  for a formed dense-stream calendar, the policy section (2.4) is the
  single place to amend.
* The staging-copy realization is pinned to the one proven form
  (SFPSHFT2 imm-0 on Round).  SFPMOV on Simple is architecturally
  plausible but unproven — deliberately outside the envelope.
