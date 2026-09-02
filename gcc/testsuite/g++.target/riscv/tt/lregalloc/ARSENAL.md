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

# LREG-allocator acceptance arsenal (lane DS)

Built INDEPENDENTLY of and AHEAD of the allocator lane (DP: DSATUR
coloring + exact-only Dst-row spill in gcc/config/riscv/tt/).  Zero
edits to pass sources; the surface is this directory + tools.  When the
allocator lands, this directory is its gate.  Lane DP's design notes
were deliberately not read: agreements here are falsifiable claims, not
coordination.

## Contract under test

1. **No-op below 9 live.**  Kernels whose max simultaneous SFPU-vector
   liveness is <= 8 must compile to byte-identical .text (allocator
   inert).  Membership in "<= 8" is machine-checked by the pressure
   oracle, not asserted.
2. **Exact-only Dst-row spill.**  Kernels with 9..16 simultaneous live
   values in a 32-bit-row Dst layout must COMPILE, and their output
   must be bit-identical to the recorded golden.  Because an exact
   spill round-trip is lossless, the output depends only on the
   dataflow DAG -- NOT on which values the allocator chooses to spill.
   The goldens are therefore legitimate regardless of allocator policy.
3. **Named refusals at the exactness/ownership edges.**
   - 16-bit Dst rows (bf16 layout evidence): a spill would truncate ->
     refuse `lreg-spill-inexact-dst-mode`.
   - No provably-free Dst row across the pressure region (moving RWC
     window, or every ownable row carrying checked live data) ->
     refuse `lreg-spill-no-free-dst`.
   Silent compilation through either edge is a gate FAIL; so is any
   unnamed error.

## Components

### 1. Real ICE victims (sfpi-victim-*.C)
Every kernel the corpus/storm record shows hitting
`lreg-pressure-exceeded`, reduced to a minimal self-contained dg
kernel that refuses TODAY (verified at nkapre/sfpi 429976b94ca and at
the pin-13 install):

| test | provenance | label |
|---|---|---|
| sfpi-victim-asinh-bh.C | literal '+' side of laneBS flagship-acosh-asinh-clean.diff (verbatim refusal in laneBS log-flagship-on-craq-final.log:51) | 10 |
| sfpi-victim-atan2-bh.C | lane S1 naive form (never committed; reconstructed per S1 DISPOSITION + sweep_2x2_ops.tsv row notes: folds/special-cases AFTER the polynomial; the shipped body's affine pre-composition undone) | 10 |
| sfpi-victim-atanh-bh.C | reconstruction from ckernel_sfpu_trigonometry.h in-source comments ("the fused form overflows it"; "a cached \|x\| - 1 variant pushed the allocator past the reload budget") | 9 |
| sfpi-victim-welford-bh.C | pre-restructure Welford: inputs held to end-of-block + accumulators + vFloat step weights (the shipped body's early trace-capture undone; commit e51ccb2c6a's in-source reason) | 12 |
| sfpi-victim-xielu-bh.C | laneBS xielu-loopheld-standalone.C (loop-held vFloat alphas), the S5 OFF-leg refusal | 9 |
| sfpi-victim-xielu-remat-bh.C | contrast twin: same body COMPILES with -mtt-tensix-optimize-const-remat (constant vs computed pressure boundary) | - |

acosh is NOT here: its pin-10 refusing form already compiles at pin 12+
(laneBS/S5 record) -- the wall moved; nothing to gate.

Future verdict: all five refusing victims must COMPILE.  Numeric
contract: their ops' existing golden/tolerance rows (the victims are
restructured-op siblings; their DAGs contain compiler-scheduled float
ops, so bit-exactness vs a hand twin is NOT claimed -- correctness
rides the existing op-level CRAQ/silicon golden gates).

### 2. Graded pressure ladder (raw-ladder*.C)
XOR-ring kernels with EXACTLY 8 / 9 / 10 / 12 / 16 simultaneously live
values (ring dependence makes the label reschedule-proof; volatile Dst
loads pin the endpoints).  XOR-only on purpose: opaque unspecs (no
folding), no CC state, format-independent, and host-exact in int32 --
the golden needs no floating-point rounding model.  No literal
constants anywhere, so const-remat/const-residency cannot relieve the
pressure (proven: rung tests assert the refusal WITH both relief flags
on).  1.0f-style creg folding (CREG_IDX_1 = lreg10) is structurally
impossible here.

Labels are machine-checked: the compiler's own prgm_const SSA pressure
model reports exactly N for every rung and exactly 8 for every
hand-spilled twin (tools/lreg_pressure_oracle.py; evidence
`~/sfpi-uplift/laneDS-evidence-20260820/oracle-labels-tip429976.txt`).

Each rung's golden = its FULL final state vector (one output row per
live value; a folded output was rejected as degenerate -- XOR
telescoping made it insensitive to row swaps).  Stimulus = splitmix32
hash of (row*37+lane) (nonlinear => no cancellation), defined in
tools/ladder_golden.py, recorded in goldens/ladder-goldens.txt.

The hand-spilled twins (raw-ladder*-spilled-bh.C) implement the SAME
DAG with explicit exact INT32 Dst round-trips at <= 8 resident values:
they compile TODAY, prove the rung is 8-implementable via exact spill,
run on CRAQ today (sim-vs-host cross-check of the golden), and are
members of the no-op byte set.

### 3. Refusal edges
- raw-bf16-ladder9-bh.C (+twin): 9-live, all Dst traffic FMT_FP16B ->
  future `lreg-spill-inexact-dst-mode`.
- raw-nofree9-rwc-bh.C (+twin): 9-live with TTINCRWC moving the window
  and stores through it inside the loop -> row identity unprovable in
  any epoch -> future `lreg-spill-no-free-dst`.
- raw-densedst9-bh.C: 64 rows all written-before/read-after the 9-live
  region.  EITHER named refusal OR compile with a spill row provably
  outside the window -- in which case the CRAQ check covers all 64
  preserved rows bit-exactly (clobber trap).
All three refuse `lreg-pressure-exceeded` today; the dg-error patterns
accept either the today or the future name so the suite stays green
across the transition, while the gate script demands the future name.

### 4. Pressure oracle + gate (tools/)
- `lreg_pressure_oracle.py`: max simultaneous liveness from the
  compiler's own dumps -- primary: rvtt_prgm_const's pre-transform SSA
  pressure line (any CFG); secondary: rvtt_lp_schedule's per-region
  `peak=` (straight-line only); third signal: refusal count at plain
  flags.  `--expect N` machine-checks a label.
- `lreg_arsenal_gate.py`: drives VERDICTS.tsv.  `--mode today`
  validates the arsenal against a pre-allocator compiler (26/26 PASS at
  429976b94ca); `--mode future` is THE acceptance gate for lane DP:
  verdict per row + label stability + .text byte-identity of the
  no-op set vs `--base-gxx`.
- `ladder_golden.py`: host-exact ladder goldens (stimulus contract
  shared with the CRAQ probe).

Example (future gate, from a build dir with $SFPI set):

    tools/lreg_arsenal_gate.py --mode future \
      --gxx <candidate-xg++> --bdir <candidate-gcc-dir> \
      --base-gxx <baseline-driver> \
      --cxxflags-extra "-isystem <target-include>/c++/15.1.0 \
        -isystem <target-include>/c++/15.1.0/riscv-tt-elf \
        -isystem <target-include>"

### CRAQ procedure -- goldens are SIM-VERIFIED
Pinned sims: craq-sim 9f324140 (bh libttsim.so sha256 32489dda..., wh
8f0079a9...).  VERIFIED 2026-08-20 on the pinned bh sim: the four
hand-spilled twins (N=9/10/12/16) and the 8-live control reproduce the
committed goldens BIT-EXACTLY, every output row, every lane (probe +
logs: tools/craq-probe/ here, full evidence
`~/sfpi-uplift/laneDS-evidence-20260820/craq/CRAQ-GOLDENS.md`).  Under
the allocator, the rungs themselves compile and must reproduce the same
rows via the same probe (modes select rung vs twin).

Harness-integration facts learned by the probe (tools/craq-probe/):
- TestConfig template parameters materialize as `constexpr` variables in
  build.h, NOT macros: kernel-side dispatch must be `if constexpr`
  (an `#if` silently compiles one arm for every variant).
- In fp32-dest-acc mode a packed result tile spans 64 sixteen-bit dst
  rows: pack arg t <-> SFPU byte addresses 64t..64t+62.  The arsenal's
  layout (in 0..30 / scratch 160..178 / out 192..222) returns in result
  tiles 0, 2 and 3.
- Raw-builtin bodies outside the eltwise wrappers need
  `math::reset_counters(SET_ABD_F)` after datacopy and an explicit
  `__builtin_rvtt_sfpencc_all_lanes()` before the body.
- The sfpi headers macro-wrap the load/store/xloadi builtins: `#undef`
  them to use the dg-test arity inside harness kernels.
- `TTSIM_TRACE_NG_TYPECAST=1` on the pinned sim prints per-SFPSTORE
  dst_row/cc/value traces (the decisive diagnostic).
- The tensor<->dst-row mapping is derived EMPIRICALLY by three
  calibration modes (identity / rowtag / lanetag=vConstTileId) -- no
  tile-geometry assumptions to go stale.

### Known holes recorded while building this (not allocator work)
- gimple-rvtt-synth.cc:553 `gcc_assert (node.used)` ICE on an sfpxor
  fold over variable-address sfploads in a rolled loop; PRE-EXISTING at
  pin 13.  12-line repro:
  `~/sfpi-uplift/laneDS-evidence-20260820/findings/ice-synth-renumber.C`.
- The prgm_const pressure model over-approximates RA at model==9 in
  some shapes (laneBS partial-asinh datapoint; reproduced here: the
  first atan2 reconstruction was model-9 yet allocated).  The oracle
  therefore reports the model number AND the behavioral refusal count;
  ladder labels use ring dependence precisely so model == RA-truth.

### Flip inventory (existing suite)
When spilling works, these six pre-existing tests flip to unexpected
PASS and need their expectations re-adjudicated by the allocator lane:
tensix/spill-diag-named-error-bh.C, tensix/spill-diag-default-bh.C,
tensix/const-remat-live-operand-nearmiss-bh.C,
tensix/const-remat-consumer-nearmiss-bh.C,
tensix/const-residency-cc-refuse-bh.C,
sfpi/dst-ownership-refuse-pressure-bh.C.
(The bf16/no-free arsenal rows must NOT flip: they pin the refusals
that survive the allocator.)
