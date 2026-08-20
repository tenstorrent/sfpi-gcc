// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// Near miss for the strict directions: the assigned zero is the
// read-only constant register (sfpi::vConst0), which SET_DEST cannot
// overwrite -- the swapped-operand keep-mask has no writable operand
// to claim.  Named refusal, bytes unchanged.  (The same spelling under
// LE/GT keeps firing: those directions never write the zero.)
// { dg-final { scan-tree-dump "ccmask refused .ccmask-zero-not-writable" "rvtt_ccmask" } }
// { dg-final { scan-tree-dump "ccmask: folds=0" "rvtt_ccmask" } }
// { dg-final { scan-assembler-not "SFPLE" } }
// { dg-final { scan-assembler-not "SFPGT" } }
#define CCMASK_FN ccmask_ltdir_cregzero
#define CCMASK_COND(x) ((x) < 0.0f)
#define CCMASK_ZERO sfpi::vConst0
#define CCMASK_X x
#define CCMASK_Y y
#define CCMASK_W w
#define CCMASK_A 0.4375f
#define CCMASK_B 1.5f
#include "ccmask-body.h"
