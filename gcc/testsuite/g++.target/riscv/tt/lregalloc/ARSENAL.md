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

# LREG-allocator acceptance tests

These tests pin the contract for an LREG allocator that colors the SFPU
vector register file (DSATUR) and spills only to Dst rows, and only when
the spill round trip is exact.  The tests are compile-only.  VERDICTS.tsv
records, for each test, the verdict of a compiler without LREG spilling
and the verdict required of the allocator; tools/lreg_arsenal_gate.py
checks a compiler against that table.

## Contract under test

1. **No-op below 9 live.**  Kernels whose max simultaneous SFPU-vector
   liveness is <= 8 must compile to byte-identical .text (allocator
   inert).  Membership in "<= 8" is machine-checked against the
   compiler's own pressure model, not asserted.
2. **Exact-only Dst-row spill.**  Kernels with 9..16 simultaneous live
   values in a 32-bit-row Dst layout must COMPILE, and their output
   must be bit-identical to the recorded golden.  Because an exact
   spill round-trip is lossless, the output depends only on the
   dataflow DAG -- NOT on which values the allocator chooses to spill.
   The goldens are therefore valid regardless of allocator policy.
3. **Named refusals at the exactness/ownership edges.**
   - 16-bit Dst rows (bf16 layout): a spill would truncate ->
     refuse `lreg-spill-inexact-dst-mode`.
   - No provably-free Dst row across the pressure region (moving RWC
     window, or every ownable row carrying checked live data) ->
     refuse `lreg-spill-no-free-dst`.
   Silent compilation through either edge is a gate FAIL; so is any
   unnamed error.

## Components

### Graded pressure ladder (raw-ladder*.C)
XOR-ring kernels (ladder-body.h) with EXACTLY 8 / 9 / 10 / 12 / 16
simultaneously live values (ring dependence makes the label
reschedule-proof; volatile Dst loads pin the endpoints).  XOR-only on
purpose: opaque unspecs (no folding), no CC state, format-independent,
and host-exact in int32 -- the golden needs no floating-point rounding
model.  No literal constants anywhere, so const-remat/const-residency
cannot relieve the pressure.  1.0f-style creg folding
(CREG_IDX_1 = lreg10) is structurally impossible here.

Labels are machine-checked: the compiler's own prgm_const SSA pressure
model (`-mtt-tensix-optimize-const-remat
-fdump-tree-rvtt_prgm_const-details`) reports exactly N for every
ladder and exactly 8 for every hand-spilled twin.  raw-ladder8-label-bh.C
scans that dump line.

Each ladder's golden is its FULL final state vector (one output row per
live value): a single folded output would be insensitive to row swaps,
because XOR telescopes.  The stimulus is the splitmix32 hash of
(row*37+lane), nonlinear so that no rows cancel; it is defined in
tools/ladder_golden.py and recorded in goldens/ladder-goldens.txt.

The hand-spilled twins (raw-ladder*-spilled-bh.C, ladder-spilled-body.h)
implement the SAME DAG with explicit exact INT32 Dst round-trips at
<= 8 resident values.  They compile without LREG spilling, show that
each ladder is implementable in the 8-register file via exact spills,
and are members of the no-op byte set.  Because the DAG is the same, a
correct allocator compiling a ladder must produce exactly its twin's
output rows.

### Tools (tools/)

- `lreg_arsenal_gate.py`: compiles every test listed in VERDICTS.tsv
  and compares the outcome with the expected verdict.  `--mode today`
  checks the verdicts of a compiler without LREG spilling (validating
  the tests themselves); `--mode future` checks the allocator contract.
  Per row it checks the verdict (compile, or refusal by name), the
  pressure label (unless `--skip-labels`), and, given `--base-gxx`,
  .text byte-identity of the `compile-noop` rows against that baseline.

- `ladder_golden.py`: host-exact ladder goldens (`--n N [--twist 0|1]
  [--trips T]`); prints one line of per-lane values per output row.

Example (allocator mode, from a build dir with $SFPI set):

    tools/lreg_arsenal_gate.py --mode future \
      --gxx <candidate-xg++> --bdir <candidate-gcc-dir> \
      --base-gxx <baseline-driver> \
      --cxxflags-extra "-isystem <target-include>/c++/15.1.0 \
        -isystem <target-include>/c++/15.1.0/riscv-tt-elf \
        -isystem <target-include>"

### Pressure-model caveat
The prgm_const pressure model can over-approximate register allocation
at a model value of 9 in some shapes: a kernel can report 9 and still
allocate.  The ladders use ring dependence precisely so that the model
equals the real pressure; for other kernels, compare the model number
with the behavioral `lreg-pressure-exceeded` refusal count.