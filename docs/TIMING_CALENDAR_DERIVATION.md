# Timing-calendar derivation: sequence words and delay bits from the row DAG

An SFPLOADMACRO descriptor consists of instruction templates, one
sequence word per macro, and a misc word.  This document sets out the
architectural facts that determine the sequence-word bit format and
the per-event delay semantics, checks them by re-deriving every proven
whole-word program in the capability tables, and specifies how a
calendar is derived for row shapes that no proven program covers.

The usual rules apply: raw words live only in the capability and
encoding tables and in test expectations; refusals are named and never
mutate code; genericity is shown by renamed-equivalent,
varied-constant and near-miss tests.

## 1. The architectural sources

**(S1) The ISA functional specification** (`SFPLOADMACRO.md`,
Blackhole ISA documentation).  Gives, normatively: the SequenceBits
per-byte format, the per-sub-unit opcode legality table, the
Simple/Round same-cycle rule, the SFPSWAP scheduling constraint, the
store field-override rules, the `Misc` field layout including
`UnitDelayKind`, the delay-counting semantics, and the
issued-instruction discard rule.

**(S2) The execution model for scheduled events.**  Absolute ready
cycle = issue + 1 + delay; per-sub-unit one-event-per-cycle queues
with earliest-ready, oldest-first arbitration; retirement before
same-cycle issue; same-cycle groups evaluated transactionally from one
snapshot with disjoint-write merging; SFPSWAP's extra claim on the MAD
comparator; the launch-latched store context (Dst row, format, layout,
but not the lane predicate, which is the live CC at store execution);
and a one-cycle CC visibility latch.  This is the model against which
formed calendars are validated, and it agrees with S1 wherever S1 is
explicit.

**(S3) The handwritten MulInt32 LLK** (`ckernel_sfpu_mul_int.h`,
`_init_mul_int_`), which programs its descriptor through the same bit
vocabulary with annotated field meanings (`UnitDelayKind`,
`UsesLoadMod0ForStore`), independently confirming the byte packing,
the 0xC template destination selector, the store byte convention, and
misc word 0x330.

These sources agree with each other and with every proven calendar
word (Sec. 3), so the bit-level sequence format is established and
per-event delays are derivable.

## 2. The established facts

All of the following are per-CPU capability-table data in
`rvtt-macro-tables.{h,cc}`.  Wormhole and Blackhole are common for all
of it.

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
  usual `VB = Imm12 & 15` aliasing, so the immediate-shift template can
  read the launch VD without the imm12 low nibble interfering.
* An event with `delay != 7` forgets any pre-existing scheduled
  instruction at the same execution time (S1).  The derivation never
  relies on this: derived schedules prove that no two events converge
  on one sub-unit in one cycle.

### 2.2 Sub-unit legality (S1, normative table)

| sub-unit | opcodes |
|---|---|
| Simple | SFPABS SFPAND SFPCAST SFPCOMPC SFPCONFIG SFPDIVP2 SFPENCC SFPEXEXP SFPEXMAN SFPIADD SFPLZ SFPMOV SFPNOP SFPNOT SFPOR SFPPOPC SFPPUSHC SFPSETCC SFPSETEXP SFPSETMAN SFPSETSGN SFPSHFT SFPSWAP(‡) SFPTRANSP SFPXOR |
| MAD | SFPADD SFPADDI SFPLUT SFPLUTFP32 SFPMAD SFPMUL SFPMULI SFPNOP (+ SFPMUL24 on BH, by S3) |
| Round | SFPNOP SFPSHFT2 SFPSTOCHRND |
| Store | SFPSTORE |

An opcode scheduled on a sub-unit that cannot execute it silently
becomes SFPNOP (S1), so wrong placement silently corrupts data.
Placement is therefore a hard architectural fact, not a heuristic: the
table is transcribed per CPU, keyed by opcode byte
(`subunit_legal_mask`), and an event whose opcode has no entry for a
free sub-unit refuses by name.

This explains why shift and copy events run on the Round sub-unit in
the sign-bit and min/max calendars: SFPSHFT2 is Round-only.  The
effects layer's `subunit` attribute classifies the source instruction,
while placement follows the template opcode that realizes it.

### 2.3 Same-cycle and adjacency constraints (S1 (†)/(‡), S2)

* (†) A Simple event and a Round event executing in the same cycle
  must split the VD16 bit: one writes LReg[16], the other does not.
* (‡) SFPSWAP scheduled on Simple executing at cycle E requires: MAD
  hosts nothing at E (SWAP claims the MAD comparator), and Simple and
  Round host nothing at E + 1.
* Each sub-unit executes at most one event per cycle, globally across
  all in-flight launches (in the steady state: modulo the row
  initiation interval).
* Same-cycle events must have disjoint architectural write sets (S1
  gives no ordering).
* An explicitly issued SFPU instruction whose sub-unit receives a
  scheduled event in the same cycle is silently discarded (S1).
  Derived schedules therefore keep explicit issues' sub-units clear of
  scheduled arrivals in their issue cycle.

### 2.4 Delay counting (S1, S3)

The 3-bit delay counts down from issue; the event executes the cycle
after the counter reaches zero (delay 0 = the cycle immediately after
the launch, i.e. issue + 1 + delay).  `Misc` bits 11:8
(`UnitDelayKind`, one bit per sub-unit) select what decrements the
counter: 0 = every cycle, 1 = every instruction the thread issues to
the SFPU.  The semantics are modal: if any outstanding scheduled
instruction is instruction-counted, all outstanding counters decrement
per issued instruction (S1).

Instruction counting exists for events whose input is produced by an
instruction issued after their launch (S1 says exactly this; S3 relies
on it: the MAD template consumes the explicit `load b` issued after
the SFPLOADMACRO).  Under a front-end stall a cycle-counted event
would execute before its forward-issued producer.

When the front end issues one SFPU instruction per cycle the two
counting models coincide; they differ only under stalls.  The
derivation rule is therefore conservative and holds under both models:

* For every event, the timing proof must hold under both counting
  models given the formed stream.
* If an event has a forward-issued input dependence, its sub-unit's
  kind bit is set to instruction counting and every issue slot in the
  descriptor's live window must be an SFPU-class instruction
  (SFPLOAD/SFPLOADI/SFPLOADMACRO/SFPNOP/compute/store; drains already
  use SFPNOP).  A window containing a non-SFPU issue (for example an
  unabsorbed TTINCRWC separator) refuses `delay-model-unproven`.
  Whether TTINCRWC decrements the instruction counter is not
  established, so the derivation does not rely on it.
* Otherwise the kind bit is cycle counting, which is stall-robust for
  events whose inputs are all latched at or before their own launch.

### 2.5 Misc word (S1, S3)

`Misc` = `UnitDelayKind[3:0] << 8 | UsesLoadMod0ForStore[3:0] << 4 |
StoreMod0[3:0]` (bits above 11 unused), packed by
`encode_misc_fields`.

* `StoreMod0`: the delayed store's data format when the store macro's
  `UsesLoadMod0ForStore` bit is clear.
* `UsesLoadMod0ForStore` bit k: macro k's store takes the launch
  word's Mod0 instead.  Derivation: set exactly the store-carrying
  macro's bit when the store's data mode equals that carrier's launch
  mode (true by construction for a merged in-place carrier and for a
  store-only carrier, whose launch encodes the store's own access);
  otherwise clear it and derive StoreMod0 from the store's typed mode.
* `UnitDelayKind`: per 2.4.

The proven whole-word misc programs set additional bits in dead
positions (see 3.4); those are functionally inert for the calendars
they accompany and are not reproduced by derivation.

### 2.6 Value visibility (S2)

* The launch's SFPLOAD executes at the launch's own issue slot; a
  scheduled event executing at cycle >= slot + 1 reads it.
* Events retire before same-cycle issues: an event executing at cycle
  E reads LReg state before any instruction issued at E writes it.
* Producer event -> consumer event needs exec(consumer) >=
  exec(producer) + 1 (same-cycle groups read one pre-write snapshot).
* CC visibility lag = 1 (`cc_visibility_lag`).  The store's lane
  predicate is the live CC at the store's execution cycle
  (`store_lane_mask_live_at_execution`; a same-cycle CC retire is not
  yet visible, an earlier-cycle one is): the ISA's launch-latched store
  overrides are exactly Addr, the Mod0 source, and the backdoor bit.
  Consequence for CC calendars: the all-lanes restore must retire
  strictly before the scheduled store executes.  A four-slot select
  calendar violates this (restore exec 3 == store exec 3) and executes
  the store under the complement mask; a compact three-slot calendar
  satisfies it (2 < 3).
* LReg[16] is a macro-only staging register: writable by compute
  events (VD16), readable only by the delayed store (VD16).  Compute
  templates cannot read it (no encodable index).

## 3. Validation: every proven word re-derived

The proven sequence programs in the tables are whole words; decoding
them with the format above (`decompose_sequence_word`,
`decode_sequence_bits`) and re-deriving each from its schedule checks
the facts of Sec. 2.  Decode notation: `unit: case/delay/VD16/route`.

### 3.1 minmax-binary (m0 = 0x00dd008c, m1 = 0x53000000)

Schedule: slot0 launch m0 (load a, VD alternating 0/1), slot1 explicit
load b (L1), slot2 launch m1 (store-only, VD3, absorbs stride); ii = 3.

* m0 Simple byte 0x8c = T0(SFPSWAP)/delay1/VD-/route1.
  SWAP consumes the explicit load b issued at slot1; events retire
  before same-slot issues, so the earliest exec is slot2 = 0+1+1.
  route=1 (VB := launch VD) shields the template's VC (= L2, the
  planned second operand) from the VC override; SFPSWAP reads VD/VC,
  so route=1 is exactly "the launch VD does not enter through VC".
* m0 Round byte 0xdd = T1(SFPSHFT2 imm-0 copy)/delay3/VD16/route1.
  This event has no source instruction; it derives from the
  store-source reachability fact.  The delayed store can read only its
  own launch VD or LReg16; the row's final value sits in macro 0's VD,
  unreachable from macro 1's store; therefore a staging copy (SFPSHFT2
  imm 0, the only Round-legal move, route=1 reading the launch VD by
  the SHFT_IMM rule, VD16 writing LReg16) is inserted.  Its delay is
  >= SWAP+1 = 3 by dependence and >= 4 by (‡) (Round must be idle the
  cycle after SWAP's exec at 2), so exec 4, delay 3.
* m1 Store byte 0x53 = store/delay2/VD16(read L16)/route0.
  exec >= copy + 1 = 5 = slot2 + 1 + 2, so delay 2.
* (‡) MAD idle at SWAP's cycle: nothing on MAD.  Steady state ii=3:
  Simple at slots ≡2, Round ≡1, Store ≡2 (mod 3); store_N and
  SWAP_{N+1} share a cycle on different units with disjoint writes.
  VD alternation follows from the copy reading VD at slot+4, after the
  next row's launch would have overwritten a shared VD.
* The copy belongs to macro 0's word (Round byte of 0x00dd008c); the
  derived model keys events by (carrier, sub-unit).

### 3.2 signbit (m0 = 0x5384004d)

Schedule: one launch per row (load, VD fixed 1), separator kept; ii=2.
Row: shift(-31) -> cast -> store.

* Round byte 0x84 = T0(SFPSHFT2 SHFT_IMM)/delay0/VD-/route1: the
  shift is Round-only (2.2), earliest exec = slot+1, route=1 shifts
  the launch VD (imm12 alias skipped), writes VD.
* Simple byte 0x4d = T1(SFPCAST)/delay1/VD16/route0: cast is
  Simple-only; consumes the shift result via VC := launch VD
  (route=0); exec = shift+1 = slot+2; VD16 stages for the store.
  The cast reads VD in the same cycle the next launch's load
  overwrites it, which is legal by retire-before-issue; this is why a
  fixed VD works here.
* Store byte 0x53 = store/delay2/VD16: exec = cast+1 = slot+3.  Note
  that "three issue slots after launch" is 1 + the field value; the
  field is 2.

### 3.3 cast-round (m0 = 0x534d0004) and the select class

* cast-round: Simple 0x04 = T0(SFPCAST)/d0/VD/route0 (VC := launch
  VD); Round 0x4d = T1(SFPSTOCHRND)/d1/VD16/route0 (VC := launch VD =
  the cast's result); Store 0x53 = d2 from L16.  The chain +1/+2/+3 is
  earliest-feasible under forced placement.
* select m0 = 0x13000004: Simple 0x04 = T0(SFPSETCC)/d0 (reads the
  condition launch VD at +1, earliest); Store 0x13 = d2/read-VD
  (bit6=0: the coalesced merge value lives in the shared launch VD;
  exec +3 = one past the last payload write at slot 2).
* select m1/m2 = 0x00000005: Simple 0x05 = T1(SFPENCC)/d0: the restore
  executes at its carrier slot + 1 and is visible one cycle later (CC
  lag).  The word does not encode the macro index.

### 3.4 Misc words

* minmax: proven 0x330, derived 0x120.  Proven bit4 (UsesLoadMod0
  for macro 0, which has no store) and bit9 (MAD kind; MAD hosts
  nothing) are dead by S1+S2; S3 shows 0x330 covers two calendar
  variants (store on macro 0 or 1) with one word.  Live bits agree:
  bit5 (in-place store takes launch mod0), bit8 (Simple
  instruction-counted, required by SWAP's forward-issued load-b
  dependence, window all-SFPU).
* signbit: proven 0x110, derived 0x010.  Live bit4 agrees; proven bit8
  (Simple instruction-counted) is not required by the derivation (the
  cast's producer is in the same macro) and its window contains the
  non-SFPU separator (2.4).
* cast-round: proven 0x100, derived 0x000.  Live bits (StoreMod0 = 0
  via bits 3:0, no launch-mod0) agree; proven bit8 as above.
* select: proven 0x706, derived 0x306.  Live bits (StoreMod0 6, no
  launch-mod0; the definition carrier's mode is free) agree.  The
  store's data is written by forward-issued payload loads, so the
  store unit's kind bit (bit11) would be required for stall
  robustness, but the proven word leaves bit11 clear and sets bits
  10:8: the handwritten select protocol relies on dense issue rather
  than kind bits.  Kind-nibble derivation is therefore a
  stall-robustness policy, not a reproduction.

Conclusion: every sequence word reproduces bit-exactly from the
schedule plus the Sec. 2 facts.  Misc words reproduce in every live
bit; dead-bit and kind-nibble conventions differ as listed.

## 4. The derivation layer

### 4.1 Position in the pipeline

Descriptor synthesis (`rvtt-macro-desc.cc`) proceeds as follows:

1. Match the proven whole-word programs first, so the shapes they
   cover keep byte-identical emission and their envelope restrictions.
   Matching includes an operand-layout probe: a program whose template
   rules reference source operands the accepted instruction does not
   have (for example the constant-register SFPSWAP variants against a
   two-register swap layout) does not match, instead of matching and
   refusing `descriptor-encoding-failed`.
2. If no proven program matches, use derived synthesis through the
   freestanding core `rvtt-macro-derive-core.h`.  Any failed
   obligation refuses by name and the scheduler falls back to its next
   carrier-grouping candidate.

### 4.2 Inputs

From the schedule: per-carrier issue slots, hosted value events (in
program order) and the delayed store, explicit issues and their slots,
the row interval, VD policy, absorbed stride.  From the effects and
tables: each event's template opcode byte (the realizing instruction:
the source's own opcode, or a table-designated realization such as
SFPSHFT2 for an explicit SFPSHFT), LReg def-use edges among row
members, and each event's launch-VD consumption position (whether the
launch value enters through VC, through the VD/VB side, or not at
all).

### 4.3 Algorithm (deterministic, refusing)

1. **Placement.**  Each hosted value event goes to a sub-unit its
   template opcode is legal on (2.2).  The legality sets are nearly
   singletons; where an opcode admits several units the first legal
   free byte in the order Simple, MAD, Round is taken.  A carrier
   whose events collide on one byte refuses
   `subunit-placement-unproven`, so the deterministic candidate search
   proceeds.
2. **Staging copies.**  If the store's data value lives neither in its
   own carrier's VD nor in LReg16 at store time, insert the staging
   copy (2.6, 3.1): SFPSHFT2 imm-0, route VB, VD16, on the producing
   carrier's Round byte (`staging_copy_realization`).  If that byte is
   occupied, or the producing value is not the producer carrier's
   launch VD (the copy can only read its own launch VD), refuse
   `store-source-unreachable`.
3. **Delays.**  Earliest-feasible, iterated to a fixpoint over: (a)
   dependence edges (2.6: producer + result latency; forward-issued
   producers: their issue slot + 1); (b) sub-unit occupancy, one per
   cycle globally, steady-state modulo ii; (c) (†) VD16 disjointness
   for same-cycle Simple/Round pairs; (d) (‡) for every SFPSWAP; (e)
   same-cycle write-set disjointness; (f) explicit issues' sub-units
   clear of scheduled arrivals (2.3); (g) store exec after every write
   to its source, and its source's next overwrite after the store.  A
   delay > 7 refuses `delay-range-exceeded`; an unsatisfiable
   constraint refuses `sequence-derivation-hazard`.
4. **Bits.**  VD16 from the planned target (LReg16 staging or final
   store staging); route from the launch-VD consumption position (VC
   consumer -> 0, otherwise 1, which also shields a planned physical
   VC); store byte source per the 3-way rule restricted to the proven
   kinds {own launch VD, LReg16} (the leave-template-VD store form is
   unproven and refuses).
5. **Words.**  Sequence bytes packed by the tables' field packer
   (`encode_sequence_bits`, `compose_sequence_word`); misc packed per
   2.5; templates through `encode_template` with fields from the
   accepted source operands or from table-designated realizations (the
   staging copy).  Delay-kind bits per 2.4 with the SFPU-dense-window
   obligation, else `delay-model-unproven`.
6. **Drain** = greatest remaining exec distance past the last issue
   slot.

Stride absorption into a derived calendar's launch auto-increment is
a per-CPU table fact (`derived_stride_absorption_proven`); a CPU
without it refuses `derived-stride-absorption-unproven`.

### 4.4 Verification

The verifier re-derives the full calendar through the shared
derivation core from the region's explicit facts, compares words
field by field, then re-checks all Sec. 4.3 inequalities from the
exchanged slots.  Because verifier and synthesis share the tables, the
reproduction tests of Sec. 6 pin the derivation core against
independently recorded words.

### 4.5 Refusal vocabulary

`subunit-placement-unproven`, `store-source-unreachable`,
`sequence-derivation-hazard`, `delay-range-exceeded`,
`delay-model-unproven`, `derived-stride-absorption-unproven`.

## 5. Derived calendars

### 5.1 Unary max/min

Row: load, SFPSWAP with a constant register, store (in place, stride
2), in a discovered loop region.  Candidate 0 (merged, ii=1): SWAP_N
executes at N+1, SWAP_{N+1} at N+2, which violates (‡) (Simple idle
the next cycle) and refuses `sequence-derivation-hazard`.  Candidate 1
(store demoted, ii=2):

* macro 0 (load carrier, alternating VD): Simple byte
  T0(SFPSWAP-cst)/delay0/route1 = 0x84; Round byte staging copy
  T1/delay2/VD16/route1 = 0xd5 (dependence gives >= 2; (‡) is
  satisfied at delay 2 since the next SWAP executes at +3, not +2).
  Word 0x00d50084.
* macro 1 (store-only carrier, sacrificial VD, absorbs the stride):
  Store byte d2/VD16 -> word 0x53000000, the same store program as the
  proven minmax m1.
* Templates: T0 packed from the accepted swap-constant source (opcode
  byte, VC = the constant register's architectural index, mod1 = the
  source's mod operand, destination selector 0xC); no hidden-write
  claim (SFPSWAP drops writes to VC >= 8).  T1 = the staging copy
  (destination selector 0xD).
* Misc: store mode == carrier launch mode -> UsesLoadMod0 bit 5; no
  forward-issued dependences -> kind nibble 0 -> 0x020.
* (‡) checks: MAD always idle; Simple/Round idle at SWAP+1 (copy at
  +3).  Same-cycle SWAP_{N+1}/copy_N at +3 satisfies (†) (the copy is
  the VD16 side).  If SWAP's write claim defers one of them a cycle,
  the dependence proof still holds: deferral only delays, and the
  store's margin is proven from programmed times.

### 5.2 Rows with more Simple events than carriers

A row such as load a, cast, load b, cast, iadd, cast, store has four
Simple-only value events.  Hosting all four on at most three carriers
refuses `subunit-placement-unproven`.  Such a row forms only when the
scheduler hosts at most one Simple event per carrier and leaves the
remainder as explicit issues placed off the scheduled Simple cycles
(2.3), with the final cast's destination planned to the store
carrier's VD.

### 5.3 MulInt32

The row (load, load, mul24, store) derives directly: MUL24 is MAD-legal
(S3) and is hosted on the store-demoted calendar as in S3's
handwritten variants; the forward-issued load-b dependence sets the MAD
kind bit with an all-SFPU window.  The derived words for the
three-address variant equal S3's macro-1 program (MAD byte 0xCC =
T0/d1/VD16/route1, store 0x5B = d3/L16; the store delay reflects the
MAD result latency of 2).

The handwritten one-slot in-place variant (`dst_index_in0 == in1 ==
out`) fails the LReg16 lifetime proof: the next row's MUL24 rewrites
LReg16 strictly before this row's store executes under both counting
models at a one-slot initiation interval.  The two- and three-slot
variants are sound, and the derivation refuses the one-slot shape.

## 6. Tests

* `rvtt-macro-derive-test.cc` (standalone, host-compiled like
  `rvtt-macro-tables-test.cc`) feeds the proven calendars' schedules
  into the derivation core and asserts the bit-exact sequence words
  (3.1-3.3), the misc live-bit agreements and listed differences
  (3.4), the S3 MulInt32 words and the one-slot lifetime refusal
  (5.3), the placement refusal of 5.2, and the derived unary max/min
  calendar (5.1).
* Proven-program shapes still resolve through the proven-program table
  first, so their byte-parity tests are unaffected.

## 7. Open questions

* Whether TTINCRWC (and other non-SFPU Tensix issues) decrement
  instruction-counted delays (2.4).  This only constrains shapes that
  need instruction counting with sparse windows, which refuse until it
  is established.
* S1's same-cycle discard of explicit issues, (‡), and (†) are proven
  statically by derived schedules rather than relied upon from any
  execution model.
* The misc kind-nibble policy reproduces no proven word bit-exactly by
  design (3.4); if kind bits turn out to matter for a dense-stream
  calendar, 2.4 is the single place to amend.
* The staging-copy realization is limited to the one proven form
  (SFPSHFT2 imm-0 on Round).  SFPMOV on Simple is architecturally
  plausible but unproven and deliberately outside the envelope.
