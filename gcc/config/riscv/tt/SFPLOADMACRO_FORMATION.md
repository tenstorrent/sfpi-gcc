# SFPLOADMACRO formation contract

This note records the target contract for compiler-generated
`SFPLOADMACRO` regions.  It is intentionally stricter than an ordinary RTL
peephole: opcode `0x93` launches instructions that are not represented by the
instruction word itself and bypass normal scoreboard arbitration.

## Motivating Blackhole sequence

The `mul_int32_generated` A/B kernel at TT-Metal `960c7b7b` compiles each row
to two typed U32 loads, followed by this 13-word explicit arithmetic sequence,
and one typed store:

```
SFPSHFT SFPSHFT
SFPMUL24 SFPMUL24 SFPMUL24 SFPNOP
SFPIADD SFPMUL24 SFPNOP SFPIADD
SFPSHFT SFPMOV SFPIADD
```

The current replay pass captures the two loads plus those 13 words and the
store/increment boundary as a 17-slot body and plays it seven more times.
Blackhole silicon measures `562.625` `MATH_ISOLATE` cycles, versus
`283.9296875` for the handwritten implementation.

The handwritten body is not a differently scheduled copy of that sequence.
Its init programs four instruction templates, four delay sequences, and the
load-macro misc register.  The row loop then uses four `SFPLOADMACRO` launches
interleaved with one explicit `SFPMUL24` and one `SFPIADD`.  Formation therefore
cannot be implemented as a `SFPLOAD; SFPMUL24; SFPSTORE` peephole, nor can the
existing 32-slot replay pass manufacture the missing parallel sub-unit work.

Pinned evidence from `/localdev/nkapre/mul-int-validation-device`:

| artifact | SHA-256 |
| --- | --- |
| generated correctness ELF | `d0d33ca4e8a672b8836297255e519e614faf4dacbcf0145d66bfe5f93f9742c3` |
| handwritten correctness ELF | `85ff0651eb7ab412238ebda5e42320d0732b0b79675bb7ab52a5724087ca59cc` |
| generated math-isolate ELF | `fa3ae69d19be64de90b6781396694735f116e911b60265a7eaa2a4d7c1ae5962` |

## Required internal representation

A naked public builtin is not a sound first step.  The selected template can
read or write arbitrary LREGs (including the transient LREG16), update CC, and
store to Dst.  Modeling only the load result would hide those effects from
IRA, DCE, scheduling, and alias analysis.

Formation must instead be post-RA and compiler-internal.  A candidate
`sfpu_macro_region` needs all of the following before it can emit:

- the ordinary load word and final physical VD;
- one instruction template per Simple, MAD, Round, and Store sub-unit;
- sequence selection and each three-bit delay;
- cycle-vs-instruction delay mode;
- every architectural LREG read/write plus the LREG16 lifetime;
- CC reads/writes and Dst memory effects;
- an issue calendar for explicit instructions interleaved with launches;
- a drain point which dominates every exit from the region;
- ownership of the selected template/sequence slots for the whole region.

The descriptor is rejected unless all effects are closed inside the region or
are represented as explicit live-in/live-out edges.

## Legality proof

The target must prove, rather than score heuristically, these invariants:

1. No two launched instructions occupy the same sub-unit in one cycle.  A
   launched instruction wins silently over an explicit instruction, so any
   collision is a miscompile.
2. At most three LREG write ports are used in one cycle.  Simple and Round
   share a port; `SFPSWAP` also borrows the MAD write port.
3. `SFPSWAP` never targets LREG16, and its writeback does not collide with MAD.
4. Mixed launched and explicit instructions are emitted as one replay-owned
   stream with `WaitForElapsedInstructions`, preserving issue lockstep.
5. No non-bitshift `SFPSHFT2` or `SFPTRANSP` occurs while launched work is in
   flight.
6. All pending work drains before a region exit.  The required tail is the
   greatest remaining programmed delay, not a fixed folklore NOP count.
7. The load/store Dst row and format remain invariant under intervening RWC
   updates.  Macro stores inherit the launch address rather than applying a
   normal store address modifier.
8. Dst/SrcS bank ownership excludes concurrent clients for the region.
9. Template, sequence, and misc state cannot be overwritten by inline asm,
   another macro region, or a call before the final drain.

## Architecture boundary

Wormhole and Blackhole encode the macro index and destination across the
`LRegInd`/address fields.  Quasar exposes a distinct `seq_id`, split VD, and a
`done` bit, and its documented dependency/structural-hazard rules still apply.
The formatter may differ by CPU, but all three CPUs use the same legality
proof.  A candidate accepted on one CPU is not automatically portable to the
others.

## Validation gate

Compiler emission remains default-off while each admitted shape is proved in
craq-sim's persistent transactional per-sub-unit delayed-event queues.  The
generic event model now executes the first clean Round-to-LReg16, Cast, and
Store chain bit-exact against the legacy signature oracle; unsupported and
dual-write shapes still fall back.  The bring-up order is:

1. general CRAQ event model and differential tests against the ISA model;
2. post-RA candidate discovery and a dump-only descriptor/verifier;
3. compile tests proving refusal at every invariant above;
4. transformed CRAQ correctness and event-calendar equivalence;
5. Blackhole silicon correctness, then device-cycle A/B;
6. Wormhole and Quasar gates before enabling either target.

Static instruction count is diagnostic only.  Device kernel cycles are the
performance acceptance authority.

## Corpus-derived design delta

Fresh semantic-C++ A/Bs add three Blackhole signals beyond MulInt32.  All
variants pass their operation's existing correctness gate.

| Body | Handwritten cycles/tile | Typed C++ cycles/tile | Typed lowering |
| --- | ---: | ---: | --- |
| binary max | 140.9296875 | 198.7578125 | four-word load/load/swap/store replay |
| binary min | 140.9296875 | 198.7578125 | four-word load/load/swap/store replay |
| addcmul | 292.921875 | 357.03125 | seven-word, one-row replay |

Binary max/min lose 41.03% because the compiler captures ordinary loads,
`SFPSWAP`, and store instead of synthesizing the production three-cycle
SFPLOADMACRO pipeline.  Addcmul loses 21.89% even though replay forms on both
sides: production interleaves two independent rows in one 14-word capture,
whereas typed C++ repeats a seven-word single-row capture and advances RWC
after every row.  TTNNWhere and MulInt32 add CC/bit-preserving selection and a
long mixed explicit/macro schedule respectively.

These are not four peepholes.  They require one periodic-region pipeline:

1. **Region recovery before replay.** Recover the repeated Dst-row operation,
   its RWC step, physical LREG effects, CC effects, and exact numerical order.
   A region may span multiple source iterations.  Do not stop candidate
   discovery at the first store: Addcmul proves that the profitable schedule
   is a two-iteration region.
2. **Dependency/resource graph.** Build true/anti/output dependencies for
   LREGs, LREG16, CC, Dst memory, and RWC.  Annotate nodes with Simple, MAD,
   Round, Load, and Store occupancy and target latency.  Predication is an
   effect edge, not an operation-name exclusion.
3. **Bounded modulo scheduling.** Search initiation intervals and unroll
   factors subject to the legality proof above.  A scheduled node may remain
   explicit or occupy a Simple/MAD/Round/Store template plus delay.  This one
   search covers Addcmul's cross-row interleaving and max/min/Where/MulInt's
   macro launches; a separate kernel-name recognizer would miss that shared
   structure.
4. **Descriptor verification and profitability.** Re-simulate the chosen
   calendar, prove all live-in/live-out and drain conditions, then compare the
   scheduled issue length (including init, tail, and RWC instructions) with
   the ordinary explicit/replay schedule.  Reject rather than weaken a proof.
5. **Emission, then replay formation.** Emit template/sequence/misc setup in a
   dominating preheader, the launch/explicit stream and computed drain, and
   explicit clobbers for all architectural effects.  Only then let the replay
   pass capture repeated launch streams.  Replay must never hide a region from
   macro analysis.

The first implementation slice should accept binary max/min and reject the
other three with exact missing-proof reasons.  The second adds multi-iteration
scheduling (Addcmul), the third adds represented CC and bit-preserving Dst
effects (Where), and the fourth admits longer mixed schedules (MulInt32).
Every slice is selected by the graph and target calendar, never by a function,
operation, source-file, or kernel name.

## Dump-only discovery scaffold

`-mtt-tensix-analyze-loadmacro -fdump-rtl-rvtt_loadmacro-details`
enables a post-RA, pre-replay observer.  It discovers maximal typed
load-to-store SFPU stretches, reports their physical instruction count, and
always prints `emit=no`.  The pass returns without changing RTL; the option is
default-off.

The rejection vocabulary is deliberately stable so corpus automation can
group blockers without parsing prose:

- `incomplete-region`
- `non-sfpu-boundary`
- `unsafe-replay-member`
- `unsupported-bulk-operation`
- `dynamic-encoding-unproved`
- `external-lreg-livein-unproved`
- `lreg-liveout-unproved`
- `cc-effect-unproved`
- `dst-rwc-effect-unproved`
- `subunit-calendar-missing`
- `simulator-event-model-missing`

The bulk-operation rejection currently covers `SFPTRANSP` and every
post-reload `SFPSHFT2` form.  This is intentionally conservative: no delayed
event semantics are inferred from an instruction name, and the scaffold does
not expose a public macro builtin.

## First opt-in executable slice

`-mtt-tensix-emit-loadmacro` additionally grants the compiler ownership of
the programmed WH/BH macro template, sequence-zero, and misc fields for a
formed function.  The option is
default-off and does not globally reserve an LREG: functions without an
eligible region remain byte-identical.  QSR remains discovery-only because
its sequence selector, split VD, and `done` encoding have not been proven
equivalent to WH/BH.

The initial emitter accepts only one structurally described four-instruction
region: one typed Dst load, logical shift right by 31, sign-magnitude cast to
the macro LReg16, and a typed Dst store.  It programs the complete template,
sequence, and misc state immediately before the launch; it never emits a
launch that depends on pre-existing state.  Three explicit SFPNOP issue slots
after the launch drain this admitted calendar's delayed store before any
following Dst access or return.  The matcher additionally proves:

- all four explicit instructions use physical L0, whose incoming value the
  load kills.  The macro launch uses L1 because the canonical SHFT2 immediate
  aliases its source selector to L1; L1 is conservatively required unused
  until multi-VD scheduling is modeled.  The dependency is closed by post-RA
  DF live-out plus a scan after the store;
- an immediately preceding `SFPENCC 3, 10` makes lane zero available while
  full-width configuration words are materialized through L0;
- load and store use the same even constant row and format with the target's
  canonical no-increment address mode (WH 3, BH 7), followed immediately by
  exact `TTINCRWC(0, 2, 0, 0)`.  Formation consumes that increment and uses
  the macro's auto-increment-by-two address modifier (WH 2, BH 6); odd rows
  alias the macro VD-high encoding bit, and rows above 1023 exceed the
  WH/BH macro's ten-bit address field;
- opcode synthesis is absent, the cast and shift modes are exact for the CPU,
  and the stored value is not live after the region;
- the function contains no source-visible SFPU configuration read or write,
  call, or inline assembly.  Raw instruction words have no typed LREG, CC,
  Dst, or configuration effects in RTL, so even constant `.ttinsn` forms are
  rejected until they carry an equivalent architectural metadata contract.

The generated launch keeps both original Dst memory operands in its volatile
RTL effect and configuration writes remain volatile barriers.  Source code
which needs to preserve or interoperate with macro slot zero must not use the
opt-in option.  Wider shapes, dual writes, counter updates other than the
proved exact absorbed increment, unknown CC state, and source-owned
configuration all retain the explicit instruction fallback.

## Exact three-slot selector emitter

`-mtt-tensix-emit-loadmacro` is a separate default-off formation gate for the
one calendar whose full delayed-event contract is now modeled and
differentially tested.  It accepts a structural three-load predicated select,
not an operation or kernel name.  The accepted region has:

- condition format 2 and payload/store format 6;
- typed all-lanes CC materialization immediately before the region;
- templates `0x7b0000c6` (Simple SETCC EQ0) and `0x8a0000d0` (Simple ENCC);
- sequences `0x13000004` and `0x00000005`, plus misc `0x706`;
- exactly three same-VD launches, encoded with raw macro address mode zero;
- an exact typed `TTINCRWC(0, 2, 0, 0)` in cycle N+3 as both the original Dst
  effect and the delayed SETCC/ENCC/Store drain.

The compiler materializes the six owned config words (including the idle
middle sequence) once immediately before
a straight-line region or once on the unique at-least-once loop-entry edge.
Calls, inline asm, source config access, multiple eligible sites, zero-trip or
non-canonical loops, wrong/missing drains, Quasar, and any encoding mismatch
retain byte-identical explicit code.  Wormhole and Blackhole emit the same
accepted launch words; target-specific ordinary-load address modes are proved
but never copied into the macro field, where the Blackhole value would overlap
`InstrMod0`.
