// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_ccmask" }
// EQ-zero widening, PRICED OFF (the WHEN-gate): the stage-B flag
// licenses the exhaustively proven two-compare keep-mask composition
// SFPOR (SFPGT (x, 0), SFPGT (0, x)) for `x == 0.0f`
// (tt/proofs/ccmask-eqne-zero/ -- the WHAT), but the delivery-cost
// gate prices the composition (six delivered words: the preserving
// copy of x, two compares, the mask combine, the standalone writable
// zero, the value merge) above the CC skeleton it removes (SETCC plus
// the predicated move), so the fold refuses by name and the CC
// lowering stands byte-identically.  Device round 6 (2026-09-03)
// measured the unpriced fold as a kernel-cycle regression on every
// EQ/NE corpus row (sign +39.8%, atan2 +10.0%); this twin pins the
// priced refusal.
// { dg-final { scan-tree-dump "ccmask refused .ccmask-eqne-fold-unprofitable" "rvtt_ccmask" } }
// { dg-final { scan-tree-dump-times "ccmask: folded zeroing CC region" 0 "rvtt_ccmask" } }
// { dg-final { scan-assembler "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPOR" } }
#define CCMASK_FN ccmask_eq_general
#define CCMASK_COND(x) ((x) == 0.0f)
#define CCMASK_X x
#define CCMASK_Y y
#define CCMASK_W w
#define CCMASK_A 0.4375f
#define CCMASK_B 1.5f
#include "ccmask-body.h"
