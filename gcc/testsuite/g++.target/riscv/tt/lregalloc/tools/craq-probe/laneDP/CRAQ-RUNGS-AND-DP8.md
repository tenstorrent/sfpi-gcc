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

# Lane DP — allocator-compiled rung CRAQ + DP-8 measured witness (2026-08-20)

Extension of lane DS's probe (copies here; installed via shim-laneDP,
farm tt-metal-pin14, toolchain laneDP-hybrid-mine = the agent/lreg-allocator
candidate cc1plus + driver, flags
`-mtt-tensix-optimize-lreg-alloc -mtt-tensix-dst-layout-32b -O2 -fno-unroll-loops`
(the arsenal rung context), sim = OWN craq clone craq-sim-laneDP @ 9f324140,
bh libttsim.so sha256 32489dda... == the reviewed pin, byte-identical).

## Rung goldens (modes 8-11 = UNSPILLED rungs, allocator-compiled)
    LADDER_GOLDEN mode=8  N=9 : 288 lanes bit-exact vs host golden
    LADDER_GOLDEN mode=9  N=10: 320 lanes bit-exact vs host golden
    LADDER_GOLDEN mode=10 N=12: 384 lanes bit-exact vs host golden
    LADDER_GOLDEN mode=11 N=16: 512 lanes bit-exact vs host golden
plus DS's hand-spilled twins (3-6) and the 8-live control (7), all
bit-exact in the same session (probe-run-final.log, 2 passed).

## DP-8 measured negative witness (modes 12 witness / 13 marker)
Same allocator-spilled mod0-0 kernel (canary at row 500 through the
runtime-resolved view; nine live loadi values force spills at scratch
offset 252 = physical rows 500..503/508..511 under the 32-bit map):
    32-bit layout (declaration TRUE):  0/128 diff lanes nonzero
    16-bit layout (declaration FALSE): 128/128 diff lanes corrupted
The declaration's violation mode is SILENT WRONG OUTPUT — now measured.

## Facts added to DS's probe notes
- SFPU bodies must be noinline (the fresh_cpp convention): inlined into
  run_kernel they merge with raw .ttinsn LLK init and the allocator
  correctly refuses the opaque region.
- -O3 GIMPLE complete peeling (untouched by -fno-unroll-loops) shreds
  the ring into ~72 short webs and exhausts the scratch pool (named
  refusal; row-reuse is a recorded future-work item) — the arsenal's
  own -O2 -fno-unroll-loops context is the parity context.
