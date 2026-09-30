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

## 4. Table interface

The capability tables expose these facts at field level:
`encode_sequence_bits` / `decode_sequence_bits` and
`compose_sequence_word` / `decompose_sequence_word` for the sequence
format (2.1), `subunit_legal_mask` and `subunit_result_latency` for
placement and result latency (2.2), `opcode_needs_swap_adjacency` for
(‡), `staging_copy_realization` for the one proven staging copy (3.1),
`encode_misc_fields` for the misc layout (2.5), and
`derived_stride_absorption_proven` for whether a calendar not taken
from a proven program may absorb the row's Dst stride into a launch's
auto-increment on a given CPU.  With these, a sequence word can be
packed from per-event fields (case, delay, VD16, route) rather than
looked up whole.

## 5. Open questions

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
