// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// Genuine near miss: `x == 0.0f` -- EQ/NE are not order tests and have
// no SINGLE-order complement, so no lone SFPGT/SFPLE keep-mask
// expresses the kept set; refuse by name.  All four order directions
// fold; equality does NOT under this flag alone.  Under
// -mtt-tensix-optimize-cc-region-general the exhaustively proven
// TWO-compare compositions (tt/proofs/ccmask-eqne-zero/) are licensed
// for EQ/NE too, but the delivery-cost WHEN-gate prices them off
// (ccmask-eqne-fold-unprofitable) -- see ccmask-zero-eq-general-bh.C /
// ccmask-zero-ne-general-bh.C; without the flag this DIRECTION refusal
// stands byte-identically, which is what this twin pins.
// { dg-final { scan-tree-dump "ccmask refused .ccmask-compare-direction-unsupported" "rvtt_ccmask" } }
// { dg-final { scan-assembler-not "SFPGT" } }
// { dg-final { scan-assembler-not "SFPLE" } }
#define CCMASK_FN ccmask_eqdir
#define CCMASK_COND(x) ((x) == 0.0f)
#define CCMASK_X x
#define CCMASK_Y y
#define CCMASK_W w
#define CCMASK_A 0.4375f
#define CCMASK_B 1.5f
#include "ccmask-body.h"
