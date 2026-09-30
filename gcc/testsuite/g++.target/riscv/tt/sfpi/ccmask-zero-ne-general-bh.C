// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_ccmask" }
// NE-zero widening, PRICED OFF (the WHEN-gate), NE direction: the
// stage-B flag licenses SFPAND (SFPLE (x, 0), SFPLE (0, x)) for
// `x != 0.0f` (tt/proofs/ccmask-eqne-zero/ -- the WHAT), but the
// delivery-cost gate prices the six-word composition above the
// two-word CC skeleton it removes, so the fold refuses by name and
// the CC lowering stands byte-identically.  Same gate and arithmetic
// as the EQ twin (ccmask-zero-eq-general-bh.C).
// { dg-final { scan-tree-dump "ccmask refused .ccmask-eqne-fold-unprofitable" "rvtt_ccmask" } }
// { dg-final { scan-tree-dump-times "ccmask: folded zeroing CC region" 0 "rvtt_ccmask" } }
// { dg-final { scan-assembler "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPLE" } }
#define CCMASK_FN ccmask_ne_general
#define CCMASK_COND(x) ((x) != 0.0f)
#define CCMASK_X x
#define CCMASK_Y y
#define CCMASK_W w
#define CCMASK_A 0.4375f
#define CCMASK_B 1.5f
#include "ccmask-body.h"
