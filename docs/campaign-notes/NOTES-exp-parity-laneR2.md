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

# Lane R2 — Exp gap ledger, re-based on execution-bound accounting (2026-08-17)

Charter §0.3/§1, work item (1): explain why Lane Z's unroll+shadow-fill
increment measured −4.1 units (99.721 → 95.596) instead of the pre-registered
−13…−18, and re-base the ledger.

## Units and provenance

- Scoreboard "cycles/tile" = mean(TILE_LOOP MATH_ISOLATE) / tile_cnt(=8) from
  the perf CSV.  The RAW mean itself is the physical cycle count of one tile's
  row loop (544 exec slots fit in 764.77 RAW cycles at ~1.03 cycles/slot plus
  overhead; they cannot fit in 95.6).  So: RAW real cycles/tile = 8 × scoreboard
  units.  All slot arithmetic below is in RAW cycles; conversions to scoreboard
  units divide by 8.
- Measured (nightly-20260817, wave pin cc1plus 658490c028e6; binaries re-derived
  byte-identically under the wave tip by this lane, see EVIDENCE.md):

  | leg | scoreboard u | RAW c/tile | .text |
  |---|---:|---:|---|
  | sem OFF | 123.719 | 989.75 | bd9bfe8954… |
  | sem ON pre-Z (nightly-16c) | 99.721 | 797.77 | d91730f39f… |
  | sem ON Lane Z | 95.596 | 764.77 | be699db268… |
  | hand | 72.468 | 579.74 | 72807b4f7e… |

- Stream inventories (from disassembly of the measured binaries):
  - hand/tile: record ttreplay 0,16,1,1 (records AND executes row 1) + 31
    playbacks → 32×16 = 512 exec slots; 4 SFPLOADI; ~8 loop scalars; RISC ≈ 73.
  - sem-Z/tile: no-exec record ttreplay 0,17,0,1 + 32 playbacks → 32×17 = 544
    exec slots; 17 record pushes (pure delivery, nothing executes); 9 SFPLOADI;
    3 TTSETC16; 1 dead li; RISC ≈ 77.
  - sem pre-Z/tile: no-exec record (18) + 32 playbacks → 576 exec slots
    (Lane U's table said 558 = 31×18; the preheader playback was dropped —
    first correction), plus 31×2 backedge scalars.

## The two decisive measurements

1. **sigmoidappx A/B (the pure-delivery control).**  Lane Z's unroll removed 64
   delivered loop-control words/tile from sigmoidappx and NOTHING else
   (6-slot capture unchanged).  Measured: 45.598 → 45.604 units (+0.006,
   noise).  Delivered words concurrent with a back-to-back replay-launch run
   are FREE — the run is execution-bound (S-model saturation), and it already
   was before the unroll: the 2-word scalar backedge fit inside each 18-slot
   row execution window with ~15 cycles to spare.
2. **exp Δ(pre-Z → Z) = −33.0 RAW.**  The increment's only execution-side
   change was capture 18→17 (SFPNOP out, shadow legally filled): −1 exec
   slot/row × 32 rows = −32 RAW.  Measured −33.0.  The backedge removal
   (62 scalars, priced 9.4 u by the old model) contributed ≈ 0, exactly as the
   sigmoidappx control says.

## Why the old model missed

Lane U's 1.23:1 model (slot-eq = replayed + 1.23×RISC) prices RISC delivery as
ADDITIVE.  During replay playback it is CONCURRENT: per-tile time =
serial preheader delivery + Σ_rows max(exec slots + interlock stalls,
1.23×delivered words), and the row term is execution-dominated (≈22 vs ≈4
cycles).  The "unexplained dynamic residual 8.6 concentrated on launch gaps"
was not delivery at all — it is execution time the slot count does not see:
intra-row pipeline (result-latency) stalls in sem's nearly fully serial
17-op chain.  Hand's row is hand-scheduled: its three independent members
(SFPLOADI 255, SFPGT, SFPAND) sit exactly in the MAD/shift latency shadows,
so hand pays ~0 stalls.

## Corrected gap decomposition (sem-Z vs hand, RAW; units in parens)

Total measured gap: 764.77 − 579.74 = 185.0 RAW = 23.13 u.

| term | RAW | units | evidence |
|---|---:|---:|---|
| capture width 17 vs 16 (net: +unfused mul/addi, +2nd EXEXP, −1 clamp) | 32 | 4.0 | slot count × 32 rows; same arithmetic that predicted Z's −33 |
| intra-row interlock stalls (unfilled latency shadows; ≈3–4 cy/row net of hand) | ≈125 | ≈15.6 | overhead accounting: sem 220.8 vs hand 67.7 RAW; sem's serial chain (load→mul→addi, cast→mad→mad, setexp→rnd→store) vs hand's filled shadows |
| per-tile setup delta (17 no-exec record pushes vs hand's record+exec overlap ≈+17; +5 loadi ≈+6; 3 SETC16 ≈+3.7; dead li +1) | ≈28 | ≈3.5 | disasm inventories |
| **total** | **185** | **23.1** | measured 23.13 |

(The stall/setup split carries ±1 u of uncertainty — a shared per-launch
turnaround term calibrated on hand (~28 RAW/tile) is common mode and cancels
in the hand-vs-sem delta; the width term is exact.)

## Re-based increment values (from 95.596 u; hand = 72.468)

1. **SETEXP fold (Lane U step 4) — IMPLEMENTED this lane**:
   setexp(man, exexp(z, Biased)) → SFPSETEXP mod1=LREG_CPY consuming z.
   Removes 1 slot/row (the 2nd EXEXP is a parallel consumer, off the critical
   spine, so a clean issue-slot removal like Z's nop): −32 RAW = **−4.0 u →
   predicted ≈ 91.6** (band 91.2–92.4; risk: none identified — hand runs the
   post-fold adjacency shft→exman back-to-back stall-free).
2. **M3 programmable-constant registers (step 3) — DESIGN this lane**: frees an
   LREG so 127.0 gets a register → the existing mul+addi→MAD combine fires:
   −1 slot AND −1 stall (the mul→addi bubble) = −2 cy/row = −64 RAW = −8.0 u;
   per-tile loadi 9→5 ≈ −5 RAW = −0.6 u.  **→ ≈ 83.0** after 1+2.
   (Old ledger priced M3 at −4.4 u; the corrected accounting nearly doubles it
   because fusion also deletes a stall.)
3. **NEW top item — interlock-stall shadow fill (schedule area, design)**:
   generalize Lane Z's fill_nop_shadows from required-nop bubbles to
   PERF bubbles (hardware-interlock latency shadows), i.e. latency-aware
   in-capture list scheduling.  Remaining stalls after 1+2 ≈ 2–3 cy/row:
   **up to −64…−96 RAW = −8…−12 u → ≈ 71–75 ≈ parity.**  This term — not
   delivery — is the real remainder of the old ledger's "launch-gap residual".
4. **Cross-tile invariant hoist (step 5) — DESIGN**: record pass + loadis +
   SETC16 trio once per kernel instead of per tile: ≈ −25 RAW ≈ −3 u, mostly
   beyond-hand headroom.

Floor check: post-1+2 sem row = 15 slots (one BELOW hand — sem's clamp is one
slot cheaper); 32×15 = 480 exec + hand-like overhead ≈ 68 RAW ⇒ ≈ 548 RAW ≈
68.5 u if all stalls are filled — parity is reachable without the macro
planner, and the planner (step 6) remains the beyond-parity path.

## Consequences for the house models

- rvtt-cost.md's 1.23:1 delivery constant is only valid OUTSIDE an active
  replay run; inside a run, delivered words are hidden up to the run's
  execution surplus.  Lane Z's ORDERING RULE (hoist prices pre-unroll) is
  unaffected, but any future pricing of scalar-word removal inside a launch
  loop must be ≈ 0, and pricing of capture-slot changes is 1 cycle/slot/row.
- The replay-launch unroll (Z's mechanism 1) remains correct and desirable:
  it removed the dead words and enabled nothing-between-launches, but its
  measured value on exp was the capture shortening (mechanism 2), 4.0 u.
# Lane R2 — designs for the remaining exp-parity increments (2026-08-17)

Companion to CORRECTED-LEDGER.md.  Increment 2 (setexp-fold) is implemented on
`agent/exp-parity-increments`; the three items below are design-only tonight,
ordered by corrected-ledger value.  All slot/cycle values are RAW cycles/tile
(scoreboard units × 8) on the exp node; hand = 579.7 RAW = 72.468 u.

## D1. M3 — programmable-constant-register allocation (−8.6 u on exp)

### Mechanics (BH sim `TENSIX_EXECUTE_SFPCONFIG`, specs/SFPCONFIG.md)
- SFPCONFIG VD=12..14 writes LReg[VD][lane] <- LReg[0][0] (L11 is the
  imm-programmed -1.0 special case).  sfpi names them CREG_IDX_PRGM1..3 /
  vConstFloatPrgm0..2.  Constraints: all lanes must be enabled (the sim
  UnsupportedFunctionality-faults on a partial CC mask), L0 is the staging
  register, one 32-bit constant costs SFPLOADI lo + SFPLOADI hi + SFPCONFIG
  = 3 delivered words, once per programming scope.
- Consumers then read the value as a constant register (sfpreadlreg PRGMn),
  exactly like CREG_IDX_1: zero allocatable-LREG pressure.

### Pass design
1. Placement: a late-gimple companion to gimple-rvtt-invariant.cc (same
   dominator/loop framework), running AFTER rvtt_combine (so mul+addi->mad has
   already been tried and pressure-refused immediates are known) and BEFORE
   expansion; flag `-mtt-tensix-optimize-prgm-const` (default off, Init(0)).
2. Candidates: loop-invariant float-typed sfp(x)loadi values that (a) the
   invariant pass hoisted but still burn an allocatable LREG across a loop, or
   (b) were refused with `Invariant SFPU immediate left in loop by LREG
   pressure` (the dump already names them: 0x42FE0000=127.0 and 0x0 on exp).
   Rank by loop depth x trip estimate x uses (reuse the replay-hoist benefit
   discipline; constants live in rvtt-cost.md).
3. Freedom proof for a PRGM register, per allocation scope (the programming
   point's dominated region):
   - no SFPCONFIG with VD in {11..14} reachable in the scope other than ours;
   - no opaque instruction region (raw `.ttinsn` asm, opaque call) anywhere in
     the function UNLESS it carries a typed effect declaration (D2) proving it
     does not write that PRGM reg or LaneConfig (VD 15) — the exp TTI init
     programs L12/L13, so WITHOUT D2 the pass must refuse exp entirely;
   - programming point must dominate all uses and sit where CC is provably
     all-lanes (function entry / kernel preheader before any pushc; the
     rvtt-cc analysis already computes region state);
   - L0 staging: the programming point must not have a live L0 — emit before
     RA (a pre-RA pass materializes into a fresh pseudo constrained to L0, as
     invariant-loadi already constrains loads).
4. Rewrite: uses of the loadi value become sfpreadlreg (PRGMn); the mul+addi
   pair whose addend was the refused immediate is re-offered to the existing
   mad combine (rerun rvtt_combine once after allocation, it is idempotent).
5. Refusals (each a dg near-miss test): CC-region not all-lanes; PRGM regs
   exhausted; opaque region without D2 declaration; non-float value (PRGM
   regs are float-typed reads); benefit below threshold; user already wrote
   vConstFloatPrgm assignments in scope (their SFPCONFIG is the other-writer
   case).
6. Exp arithmetic (post setexp-fold): moving 1/ln2 and the c2/c1 coefficient
   pair frees 2 allocatable LREGs -> 127.0 gets a whole-loop register -> mad
   fusion (-1 slot, -1 stall = -64 RAW) and per-tile loadi 9->5 (-5 RAW).
   Note exp's own init ALREADY programs L12=1/ln2, L13=c2; with D2 declaring
   that, M3's allocator can simply bind to the already-programmed values
   (zero programming cost) — without D2 it must refuse.  M3's first
   measurable exp win therefore lands together with D2.

### Gates
Fire (renamed/varied constants, both archs), near-misses above, flags-off
byte-identity, corpus classification (expect exp sem ON changed only when
combined with D2), paired CRAQ (SFPCONFIG semantics are sim-modelled).

## D2. Region-scoped typed effects for opaque TTI regions (unblocks M3 + D4)

The single refusal `function has opaque LREG state` (invariant-loadi) and
`row-opaque-effect` (macro-planner) come from raw `.ttinsn` pushes with no
effect model.  rvtt-insn.def already defines the metadata builtin
`sfprawlreg_access (release_mask, write_mask)` — but it only covers LREGs and
is function-local folklore.

Design: a scoped declaration pair the LLK headers can emit around a raw
region:
- `__builtin_rvtt_ttregion_begin (effects_id)` / `__builtin_rvtt_ttregion_end`
  where effects_id keys a small table: LREGs written, LREGs read,
  PRGM/LaneConfig config words written (SFPCONFIG VD set), ADDR_MOD/SETC16
  config words written, CC effect, Dst/RWC effect.  (Encoding: reuse the
  effect fields already enumerated by the generated attribute family in
  rvtt-cost.md — subunit/latency excluded, architectural effects only.)
- Consumers: invariant-loadi (drop the function-global opacity refusal to
  region-scoped: hoists may cross a declared region that provably does not
  touch the hoisted value's registers), M3 freedom proof, macro-planner
  row admission (the WP8 §6b step-1 discovery extension), and D4's crossing
  rules.
- Soundness: the declaration is TRUSTED like sfprawlreg_access is today; the
  CRAQ gate is the check.  An undeclared region keeps today's byte-identical
  refusal.  Declarations for `_init_exponential_tti_bf16_` (writes L12, L13,
  ADDR_MOD_6, SETC16 trio; all-lanes; no Dst) belong to the tt-metal side and
  need a coordinated kernel-header increment — outside this lane's territory
  tonight (shared tree), flagged for the wave planner.

### Cross-tile invariant hoist (Lane U step 5, ~−3 u)
With D2 in place the tile-loop preheader contents (record pass, 5 remaining
loadis, SETC16 trio) hoist once per kernel: the blockers are exactly the
declared init region and the MOP/stallwait wait — the record pass may not
cross the stallwait (the replay engine and math thread handshake there), so
the hoist target is the region between the init and the first tile, with the
existing replay-hoist legality machinery (rtl-rvtt-replay.cc) extended from
"loop preheader" to "dominating block after the last declared-opaque config
write".  Refusals: multiple tile loops with different captures, capture
registers written between tiles, undeclared opaque region in between.

## D3. Interlock-stall shadow fill (NEW top ledger item, −8…−12 u; audit-first)

The corrected ledger's dominant term is ~3-4 cycles/row of hardware
result-latency stalls inside sem's replayed row (hand fills these shadows by
hand).  Lane Z's fill_nop_shadows only moves fillers into REQUIRED-nop
bubbles (xtt_delay `dynamic`); the stalls at issue are transparent hardware
interlocks the current machine model does not price at all —
`xtt_result_latency` is audited ONLY for the mad family (stored 2 = latency 1;
rvtt-cost.md F1.3 note).

Plan, strictly audit-first:
1. **Latency audit (measured, not guessed)**: extend the BH sim (or a silicon
   microbench under the §1 protocol) to report per-instruction-class
   result-ready distance: for each producer class P in {load, mul/mad, addi,
   swap, exexp, exman, shft, cast, setexp, stochrnd}, a 2-instruction
   dependent pair inside a replayed capture, timed at 32 trips, vs the same
   pair separated by an independent filler.  Output: audited
   `xtt_result_latency` values for the ~10 classes exp/sigmoid/minmax use.
   (The three measured exp points already pin the SUM ≈ 3-4 cy/row; the audit
   distributes it.)
2. **Scheduler extension** (rtl-rvtt-schedule.cc, same flag family as
   latency-schedule): after fill_nop_shadows, a windowed list-scheduling pass
   over replay-capture bodies and straight-line SFPU runs that reorders
   AUDITED-safe independent members (the existing XTT_LATENCY_REORDER_SAFE +
   effects-audited crossing rules from Lane Z's mechanism 2 apply unchanged)
   to minimize modeled interlock stalls.  Refuse byte-identically when any
   member is unaudited — with only the mad family audited today the pass
   would fire on nothing, which is the correct starting state; value arrives
   class-by-class as audits land.
3. Exp expectation once load/mul/cast/round classes are audited: the row has
   3 independent-or-reorderable members (the sfpmov filler, the 255 loadi if
   M3 has not consumed it, the stochrnd/store tail vs the next row's head —
   NOTE cross-row filling requires capture-rotation, a deliberate follow-up),
   recovering most of the 2-3 residual stalls/row.

Risks: mis-audited latency = wrong-but-CRAQ-clean only if the sim models the
interlock (it must, or CRAQ itself would be wrong today) — CRAQ remains the
correctness gate and silicon the timing gate.
