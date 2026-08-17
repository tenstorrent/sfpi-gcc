// Wormhole twin of mop-form-run-bh.C: identical MOP formation over the
// hoisted-and-unrolled launch run (encodings and MOP config registers
// are WH/BH-identical table facts).  WH's capture is 8 slots (its
// per-row lowering is wider than BH's); the MOP delivery form is the
// same 8-iteration TTMOP.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -mtt-tensix-optimize-mop-form -mtt-tensix-mop-form-force -fdump-rtl-rvtt_mop_form-details" }
// { dg-final { scan-rtl-dump-times "MOP formed \\(mop0-lA-replay, run\\): 8 iterations of launch \\\[0,\\+8\\) -> TTMOP 0, 7, 0" 1 "rvtt_mop_form" } }
// { dg-final { scan-assembler-times "TTMOP\\t0, 7, 0" 1 } }
// { dg-final { scan-assembler-times "TTMOPCFG\\t0" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY" 1 } }

#include "replay-counted-body.h"
