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

Compiler emission remains default-off until craq-sim executes arbitrary
template/sequence combinations with real per-sub-unit delayed-event queues.
Its current known-signature shortcuts are useful for checking existing LLKs,
but cannot validate a novel compiler schedule.  The bring-up order is:

1. general CRAQ event model and differential tests against the ISA model;
2. post-RA candidate discovery and a dump-only descriptor/verifier;
3. compile tests proving refusal at every invariant above;
4. transformed CRAQ correctness and event-calendar equivalence;
5. Blackhole silicon correctness, then device-cycle A/B;
6. Wormhole and Quasar gates before enabling either target.

Static instruction count is diagnostic only.  Device kernel cycles are the
performance acceptance authority.

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
