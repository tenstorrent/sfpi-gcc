<!-- Copyright (C) 2026 Tenstorrent Inc.

     This file is part of GCC.

     GCC is free software; you can redistribute it and/or modify it
     under the terms of the GNU General Public License as published by
     the Free Software Foundation; either version 3, or (at your
     option) any later version.

     GCC is distributed in the hope that it will be useful, but WITHOUT
     ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
     or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
     License for more details.

     You should have received a copy of the GNU General Public License
     along with GCC; see the file COPYING3.  If not see
     <http://www.gnu.org/licenses/>.  -->

# SFPLOADMACRO formation contract

This note records the target contract for compiler-generated
`SFPLOADMACRO` regions.  It is intentionally stricter than an ordinary RTL
peephole: opcode `0x93` launches instructions that are not represented by the
instruction word itself and bypass normal scoreboard arbitration.

## Motivating sequence

A Blackhole 32-bit integer multiply kernel compiles each row to two typed
U32 loads, followed by this 13-word explicit arithmetic sequence, and one
typed store:

```
SFPSHFT SFPSHFT
SFPMUL24 SFPMUL24 SFPMUL24 SFPNOP
SFPIADD SFPMUL24 SFPNOP SFPIADD
SFPSHFT SFPMOV SFPIADD
```

Replay formation captures the two loads plus those 13 words and the
store/increment boundary as a 17-slot body and plays it seven more times.

An equivalent hand-written `SFPLOADMACRO` body is not a differently
scheduled copy of that sequence.  Its init programs four instruction
templates, four delay sequences, and the load-macro misc register.  The
row loop then uses four `SFPLOADMACRO` launches interleaved with one
explicit `SFPMUL24` and one `SFPIADD`, and runs in about half the cycles.
Formation therefore cannot be implemented as a `SFPLOAD; SFPMUL24;
SFPSTORE` peephole, nor can the 32-slot replay buffer manufacture the
missing parallel sub-unit work.

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
   greatest remaining programmed delay, not a fixed NOP count.
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

## Validation

Formation is disabled by default.  A formed region must agree with an
executable model of the per-sub-unit delayed-event queues as well as with
the ISA model, and every invariant above needs a compile test showing
that its violation refuses formation.

Static instruction count is diagnostic only; profitability is judged by
device kernel cycles.

## Periodic-region pipeline

Other periodic SFPU bodies need the same machinery: binary max and min
(load, load, `SFPSWAP`, store) where production code uses a three-cycle
`SFPLOADMACRO` pipeline; multiply-accumulate bodies whose best schedule
interleaves two independent rows in one capture instead of advancing RWC
after every row; predicated select, which needs CC and bit-preserving Dst
effects; and long mixed explicit/macro schedules such as the multiply
above.

These are not separate peepholes.  They require one periodic-region
pipeline:

1. **Region recovery before replay.** Recover the repeated Dst-row operation,
   its RWC step, physical LREG effects, CC effects, and exact numerical order.
   A region may span multiple source iterations.  Do not stop candidate
   discovery at the first store: the profitable schedule for a
   multiply-accumulate body is a two-iteration region.
2. **Dependency/resource graph.** Build true/anti/output dependencies for
   LREGs, LREG16, CC, Dst memory, and RWC.  Annotate nodes with Simple, MAD,
   Round, Load, and Store occupancy and target latency.  Predication is an
   effect edge, not an operation-name exclusion.
3. **Bounded modulo scheduling.** Search initiation intervals and unroll
   factors subject to the legality proof above.  A scheduled node may remain
   explicit or occupy a Simple/MAD/Round/Store template plus delay.  This one
   search covers cross-row interleaving and macro launches alike; a
   per-kernel recognizer would miss that shared structure.
4. **Descriptor verification and profitability.** Re-simulate the chosen
   calendar, prove all live-in/live-out and drain conditions, then compare the
   scheduled issue length (including init, tail, and RWC instructions) with
   the ordinary explicit/replay schedule.  Reject rather than weaken a proof.
5. **Emission, then replay formation.** Emit template/sequence/misc setup in a
   dominating preheader, the launch/explicit stream and computed drain, and
   explicit clobbers for all architectural effects.  Only then let the replay
   pass capture repeated launch streams.  Replay must never hide a region from
   macro analysis.

Every accepted shape is selected by the graph and target calendar, never
by a function, operation, source-file, or kernel name.
