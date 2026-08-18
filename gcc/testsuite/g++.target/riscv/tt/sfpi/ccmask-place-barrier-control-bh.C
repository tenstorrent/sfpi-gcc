// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant" }
// Control for ccmask-place-bh.C: with the fold off, the v_if's
// CC-setting compare is a loop-scoped SFPU barrier and the invariant
// pass refuses the whole loop's immediate hoists.
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }
#define CCMASK_FN ccmask_place_ctl
#define CCMASK_X x
#define CCMASK_Y y
#define CCMASK_W w
#define CCMASK_A 1.328125f
#define CCMASK_B (-0.15625f)
#include "ccmask-body.h"
