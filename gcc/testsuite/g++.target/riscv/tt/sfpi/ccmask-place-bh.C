// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_ccmask -fdump-tree-rvtt_invariant" }
// Check that after the ccmask fold the invariant pass hoists the loop's immediates.
// { dg-final { scan-tree-dump-times "ccmask: folded zeroing CC region" 1 "rvtt_ccmask" } }
// { dg-final { scan-tree-dump "Hoisted invariant SFPU immediate" "rvtt_invariant" } }
#define CCMASK_FN ccmask_place
#define CCMASK_X x
#define CCMASK_Y y
#define CCMASK_W w
#define CCMASK_A 1.328125f
#define CCMASK_B (-0.15625f)
#include "ccmask-body.h"
