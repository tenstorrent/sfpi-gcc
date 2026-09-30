// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_ccmask -fdump-tree-rvtt_invariant" }
// The fold removes the loop's only CC statement -- the loop-scoped
// barrier that forces the invariant immediate hoist to refuse the
// whole loop -- so the invariant pass running next hoists the loop's
// immediates under its own pressure-bounded selection.  The control
// (ccmask-default-off-bh.C with -mtt-tensix-optimize-invariant-loadi)
// keeps the barrier and hoists nothing from this loop.
// { dg-final { scan-tree-dump-times "ccmask: folded zeroing CC region" 1 "rvtt_ccmask" } }
// { dg-final { scan-tree-dump "Hoisted invariant SFPU immediate" "rvtt_invariant" } }
#define CCMASK_FN ccmask_place
#define CCMASK_X x
#define CCMASK_Y y
#define CCMASK_W w
#define CCMASK_A 1.328125f
#define CCMASK_B (-0.15625f)
#include "ccmask-body.h"
