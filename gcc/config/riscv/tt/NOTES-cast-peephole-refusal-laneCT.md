<!-- Copyright (C) 2026 Tenstorrent Inc.

     This file is part of GCC.

     GCC is free software; you can redistribute it and/or modify it under
     the terms of the GNU General Public License as published by the Free
     Software Foundation; either version 3, or (at your option) any later
     version.

     GCC is distributed in the hope that it will be useful, but WITHOUT ANY
     WARRANTY; without even the implied warranty of MERCHANTABILITY or
     FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
     for more details.

     You should have received a copy of the GNU General Public License
     along with GCC; see the file COPYING3.  If not see
     <http://www.gnu.org/licenses/>.  -->

# laneCT: proof-carrying peephole harvest — castfp32tofp16a REFUSAL record

Lane CT, 2026-08-19/20. Branch `agent/cast-peephole-harvest` off `e0754714a5b`.
Implements the adjudication half of proposal P2 (Minotaur/Souper/Hydra-lineage
proof-carrying peephole harvest, `~/sfpi-uplift/laneCO-evidence-20260820/
PROPOSALS.md`) for its flagship target. **No rule ships from this lane: the
proof obligation is affirmatively REFUTED by the exhaustive oracle.** This
note is the named-refusal record that P2's cache design calls for, so the
same cut is never re-mined.

## The cut (derived, not assumed)

Fresh body `tt-metal tests/helpers/include/fresh_cpp/castfp32tofp16a.h`
compiled at the pin-13 toolchain (`-O3 -mcpu=tt-bh-tensix`), gimple after
`275t.optimized` (proof artifact `proofs/cast-fp16a-rne/` carries the dump):

    x  = sfpload
    s  = sfpshft_i (x, -13)              ; >>13 logical
    l  = sfpand (s, loadi 1)             ; kept LSB
    t  = sfpiadd_v (sfpiadd_v (x, loadi 0x0FFF), l)
    r0 = sfpand (t, loadi 0xE000 sign-extended = 0xFFFFE000)
    e  = sfpand (x, loadi-upper 0x7F80)  ; exponent field
    pushc; vif; sfpxicmps (e, 0x7F800000, eq); condb
    r  = sfpassign_lv (r0, x)            ; non-finite passthrough
    popc; sfpstore (r)

18 issue words per row after replay capture (observed TTREPLAY 0,18 at
pin-13). Per-lane denotation over the u32 bit pattern:

    cut(u) = (u & 0x7F800000) == 0x7F800000
               ? u
               : (u + 0x0FFF + ((u >> 13) & 1)) & 0xFFFFE000

i.e. round-half-to-EVEN on the 13 dropped mantissa bits, exponent kept in
fp32 range, denormals rounded as plain bit patterns (sign preserved),
Inf/NaN (payload included) passed through. This is bit-identical to the
harness golden `python_tests/helpers/golden_generators.py:_cast_fp32_to_fp16a`.

## The capability-table replacement form (derived from the tables)

The hand kernel's one-slot form is `sfpi::convert<vFloat16a>(x,
RoundMode::Nearest)` = `__builtin_rvtt_sfpstochrnd_i (…, imm8=0,
mod1=SFPSTOCHRND_MOD1_FP32_TO_FP16A=0, mod0/rnd=SFPSTOCHRND_RND_EVEN=0)`
(`rvtt-insn.def:253-256`, typed effects `rvtt.md:2986-3023`, subunit
"round"). ISA encoding: opcode 0x8E, instr_mod1[2:0]=0 selects FP32_TO_FP16A,
bit21 rnd_mode=0 selects the deterministic sample path.

## The proof obligation and its refutation

Oracle: craq-sim @ 9f324140 (pinned), `src/tensix.cpp:9508-9545`
(TENSIX_EXECUTE_SFP_STOCH_RND, mode==0 arm) + `:2772-2816` (srnd helpers);
the SFPLOADMACRO template copy at `:11497-11581` is a literal duplicate.
Exhaustive 2^32 sweep (harness + result in `proofs/cast-fp16a-rne/`,
32 s on host):

    total mismatches 33,810,429 of 4,294,967,296 — NOT EQUAL
      260,096   tie-even: discarded == 0x1000 with even kept-LSB.
                rnd_mode=0 substitutes sample=STOCH_MIDPOINT(0x80) and
                srnd_round_up_sample uses >= (tensix.cpp:2810-2814, the >=
                "matching RTL" per :3242-3245) => round-half-AWAY. The cut
                (and the golden) are round-half-to-EVEN. Exact count
                2 signs x 254 exps x 2^9. THIS CLASS IS ON PLAIN FINITE
                NORMALS: it also kills any guard-preserving variant that
                replaces only the rounding core.
      16,769,022 denormal values: hw flushes exp==0 to +0 (tensix.cpp:9526);
                the cut rounds the bit pattern (may even carry into the
                smallest normal).
      4,097     sign lost: -0.0 and small negative denormals -> hw +0.
      16,777,214 NaN payloads (=2*(2^23-1) exact): hw normalizes exp==255
                to signed Inf (src &= 0xFF800000); the cut passes payloads.
                P2's premise "the hw convert subsumes the Inf/NaN guard" is
                false for every NaN.
      0         other (classification complete).

    cut-stream sha256 70608a8835321ce609bb64c6c2688831b971d393d0b7656c0565d3a98d8f3bec
    hw-stream  sha256 600148d0715697a3dac5fdfbfb201c05d635d5940c6f1c3bd07e750b39e5e12b
    mismatches sha256 fda38ab873d374aee91447077148812cb17f6c9ee478e01b5f99aad2a1171e70

Refusal name for any future matcher attempting this cut:
**`cast-cut-equivalence-refuted`** (a stronger fact than the generic
fail-closed `cast-cut-proof-missing`: the proof does not merely not exist,
its negation is machine-checked).

## Why the hand kernel "passes" anyway (the naming trap)

`sfpi_lib.h:651-657`: on WH/BH `RoundMode::Nearest` aliases `NearestAway`
yet `stochrnd_rnd` maps it to the encoding *named* `SFPSTOCHRND_RND_EVEN`
(=0), whose RTL-matched behavior is half-away. The golden's comment claims
`convert<vFloat16a>` is round-to-nearest-even — contradicted by the pinned
oracle. The hand kernel and the golden therefore disagree on 33.8M inputs;
the harness never observes it (see the laneCT evidence for the comparison /
input-domain analysis). The +378% board row is a CONTRACT question (golden
model vs hardware semantics — eqz/-0.0 Option A precedent), not a compiler
transform opportunity: no sound rewrite of this fresh source can reach the
one-slot form.

## What would close the row (owner decision, not a compiler rule)

1. Re-spec the golden (and fresh body) to the hardware convert's semantics
   (half-away ties, denormal->+0, NaN->signed Inf); the fresh body then
   legally becomes `sfpi::convert<vFloat16a>(x, RoundMode::Nearest)` — a
   storm-lane source rewrite, ~15.9 c/t, no compiler change at all; or
2. Keep the RNE contract, accept the fresh cost (a true software-RNE floor);
   the compiler-side levers left are generic (const-residency hoisting of
   the four SFPLOADIs — laneCE refusal #1 — and replay/loop mechanics),
   worth a few slots, never 18 -> 1.

## Method note (what P2 gets right)

The proof-carrying discipline did exactly its job: an op-name/pattern
trigger would have shipped a wrong-code rule that silently changes 33.8M
results; the denotational gate refused it for the price of a 32-second
sweep. Rule admission by machine-checked equivalence against the pinned
oracle remains the right mechanism; this record simply moves its flagship
target from "attack" to "refuted premise", and the proof harness under
`proofs/` is the reusable template for the surviving sibling candidates
(absint32, unaryshift/SM32 conversion-in-load — each per-shape, each with
its own sweep).
