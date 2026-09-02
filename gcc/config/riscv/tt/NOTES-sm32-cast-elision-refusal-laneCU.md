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

# laneCU: proof-carrying peephole harvest — leftshift SM32 cast elision REFUSAL record

Lane CU, 2026-08-20. Branch `agent/int-peephole-harvest` off laneCT's
`fc70df6a87b4a` (itself off pin-14 seed `e0754714a5b`).  Implements the
adjudication half of proposal P2 for the `leftshift` sibling candidate
(+19% vs hand) that laneCT scoped.  **No rule ships for this cut: the
proof obligation is affirmatively REFUTED by the exhaustive oracle.**
This is the named-refusal record so the cut is never re-mined.

## The cut (derived, not assumed)

Fresh body `calculate_left_shift_fresh_cpp` (tt-metal
`tests/helpers/include/fresh_cpp_operations.h`), compiled at the pin-13
toolchain (`-O3 -mcpu=tt-bh-tensix`; emission in
`proofs/sm32-cast-elision-shift/cut-emission-pin13.txt`): a 12-word
replayed row

    SFPLOAD L0 mod0=4; SFPCAST L0,L0,3; SFPLOAD L1 mod0=4; SFPCAST L1,L1,3;
    SFPSHFT L0,L1,0,0; SFPSETCC L1,0,4; SFPIADD L1,L1,-32,1; SFPCOMPC;
    SFPMOV L0,L9,0; SFPENCC 3,10; SFPCAST L0,L0,3; SFPSTORE L0 mod0=4

The three SFPCAST mod1=3 words are the BH lowering of
`sfpi::DataLayout::SM32` (sfpi_funcs.h: on BH, SM32 = software
smag<->int conversion around a raw mod0-4 access; `smag_to_int` /
`int_to_smag` = `__builtin_rvtt_sfpcast(..., mod1=3)`, the self-inverse
sign-preserving conditional negate).  The hand kernel
(`ckernel_sfpu_shift.h calculate_binary_left_shift`) is the same
9-word core with `InstrModLoadStore::INT32_2S_COMP` conversion-in-load
and NO casts — laneCE item 4's "12 vs 9" slots.

## The proposed rule and its proof obligation

Elide the three SFPCASTs (equivalently: rewrite the SM32 access chain
to the INT32_2S_COMP conversion-in-load form).  On BH, INT32_2S_COMP
loads and stores are architecturally inert — raw 32-bit Dst moves
(craq-sim @ 9f324140 tensix.cpp:8465-8466 load mod0 4==12 raw on
TT_VERSION==1; :8656-8658/8670-8676 store raw; the laneCI
INT32_2S_COMP-inertness fact, machine-visible in the oracle source) —
so the elided chain equals the hand chain, and the obligation is

    forall w, a:  cast3( inrange(cast3(a)) ? shft(cast3(w), cast3(a)) : 0 )
               == inrange_raw(a) ? (w << (a & 31)) : 0

## Refutation (exhaustive; harness + result in proofs/sm32-cast-elision-shift/)

- PART A (amount dimension, all 2^32 a): EQUAL — both selectors and
  effective shift counts agree everywhere (cast3 is the identity on
  sign-clear amounts; both arms zero every sign-set or >=32 amount).
  Every divergence therefore lives in the value dimension.
- PART B (stratified-exhaustive, k = 0..31, full 2^32 values each):
  **NOT-EQUAL: 92,341,796,868 mismatches**, in exactly two classes
  (partition by bit31(w); `other = 0` by construction), with every
  per-k count matching its closed form exactly:
    - P (sign-clear w): the product crosses into bit31 and the final
      int->smag cast rewrites it: count(k) = 2^30 - 2^k  (k=1..30).
      Example k=1: w=0x40000001, elided 0x80000002, fresh 0xFFFFFFFE.
    - N (sign-set w): the load cast negates the magnitude before the
      shift: count(k) = 2^31 - 2^(k-1) - max(2^(k-1)-1, 0) - 1 (k>=1).
      Example k=1: w=0x80000001, elided 0x00000002, fresh 0x80000002.
  The elision is sound ONLY at k = 0 (cast3 self-inverse) and k = 31
  (both arms collapse to {0, 0x80000000}) — a precondition family far
  too thin to carry a rule.
  Census sha256 7425f8ed91f991301ecf67e4e888a2c341b54e27a4f81df3347a4cb87629ecea.

Refusal name for any future matcher attempting this cut:
**`sm32-cast-elision-refuted`**.

## What the refutation means (the contract observation)

The fresh SM32 body and the hand INT32_2S_COMP body are DIFFERENT
functions on 3/4 of the per-lane input space: the fresh source
interprets the Dst word as sign-magnitude, the hand kernel as raw two's
complement.  Both pass the harness golden — the corr domain never
distinguishes them.  This is the laneCT castfp32tofp16a situation
again: the +19% row prices conversion machinery whose semantics the
harness never observes, and WHICH ARM IS RIGHT on negative operands is
a golden-contract question (owner decision), not a compiler transform.
If the golden's Dst contract is raw two's complement (the hand
kernel's reading, and the BH-native one), the fresh body's fix is a
SOURCE rewrite to `DataLayout::I32` — the exact rewrite storm-S5
already applied to unaryshift (`fresh_cpp/unaryshift.h`), worth all
three cast slots with zero compiler change.

## Method note

Same P2 lesson as laneCT: the ISA-name plausibility ("SM32 is the same
INT32_2S_COMP contract" — the fresh body's own comment claims it) does
not survive the oracle.  The proof discipline refused a wrong-code
rule that would silently change ~92 billion (w, k) results, for the
price of a ~7-minute sweep.
