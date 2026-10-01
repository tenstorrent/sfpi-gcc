// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant" }
// Control for ccmask-place-bh.C.  RE-RECORDED (during
// structured-CC-restore): with the fold off, the v_if's CC-setting
// compare used to be a loop-scoped SFPU barrier refusing the whole
// loop's immediate hoists; the restore proof now discharges that
// barrier (balanced plain-PUSHC/POPC region, narrowing-only
// modifiers), so the invariant immediates hoist here too.  The ccmask
// fold's value is the CC-region removal itself, no longer
// hoist-enablement; the placement assertions stay in
// ccmask-place-bh.C.
// { dg-final { scan-tree-dump "Hoisted invariant SFPU immediate" "rvtt_invariant" } }
#define CCMASK_FN ccmask_place_ctl
#define CCMASK_X x
#define CCMASK_Y y
#define CCMASK_W w
#define CCMASK_A 1.328125f
#define CCMASK_B (-0.15625f)
#include "ccmask-body.h"
