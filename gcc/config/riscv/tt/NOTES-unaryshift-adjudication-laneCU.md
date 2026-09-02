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

# laneCU: proof-carrying peephole harvest — unaryshift adjudication record

Lane CU, 2026-08-20.  Branch `agent/int-peephole-harvest`.  Adjudicates
the `unaryshift` sibling candidate (+51.5% vs hand) that laneCT scoped
from laneCE item 4 ("SM32 lowering wastes 3 slots/row, 12 vs 9").
**Verdict: the scoped cut no longer exists — the premise is STALE.**
Record name: **`unaryshift-cut-collapsed-already-hw`**.

## The finding (derived at pin-13, both arms)

The laneCE analysis predates storm-S5.  The current fresh body
(`fresh_cpp/unaryshift.h`, S5's lift) already uses
`DataLayout::I32` — mod0-4 raw loads/stores, NO conversion casts — and
the production hand kernel (`ckernel_sfpu_unary_shift.h
calculate_left_shift`) is itself typed SFPI with the same layout.  At
pin-13 both compile the in-range leg to the IDENTICAL row stream
(`proofs/shft-imm-vs-reg/row-streams-pin13.txt`):

    SFPLOAD L0, 0, 4, 7
    sw a5, 0(a4)        # 1:SFPSHFT L0, L0, a5, 7   (RISC-patched dyn-imm)
    SFPSTORE L0, 0, 4, 7
    TTINCRWC

There is no dataflow difference for a P2 rule to close: no conversion
slots, no CC region, no capability gap.  The 2x2 row's residual loss is
NOT a cut-class loss; it lives in delivery/schedule structure (the
production body pins `#pragma GCC unroll 8`, the fresh body is a free
loop; and see below).

## The real residual: per-row RISC-pushed dynamic immediate

Both arms pay one RISC `sw` per row to patch the SFPSHFT immediate
(the shift amount is a runtime scalar), which prices at ~1.23x a
replayed slot AND blocks replay capture of the row — the fresh
absint32 row replays, this one cannot.  The generic fix is a
formation/delivery mechanism, not a peephole: materialize the
loop-invariant amount ONCE (preheader SFPLOADI, itself RISC-patched
once) and switch the row to the register-form SFPSHFT, making the row
static and replayable.

That mechanism's denotational obligation is discharged IN ADVANCE:
`proofs/shft-imm-vs-reg/` sweeps the dynamic-immediate form against
the register form per in-range amount k in [0,31], full 2^32 values
each — **EQUAL, 0 mismatches in every stratum** (both lower to the
same craq-sim SFPSHFT arm, tensix.cpp:8931-8968; the out-of-range leg
is a host-scalar branch in both arms).  A future scheduler/formation
lane can cite this proof directly; the remaining work there is LREG
pressure accounting and preheader placement, not semantics.

## INT32_2S_COMP-inertness survival (per the task obligation)

Both arms' loads and stores are mod0=4 (INT32_2S_COMP), raw on BH
(craq-sim tensix.cpp:8465-8466, :8656-8658) — the shape never relies
on a conversion the hardware does not perform; laneCI's inertness
finding is survived trivially because nothing here converts at all.
Contrast the leftshift record
(NOTES-sm32-cast-elision-refusal-laneCU.md), where the SM32 body's
casts are real and the elision is refuted.
