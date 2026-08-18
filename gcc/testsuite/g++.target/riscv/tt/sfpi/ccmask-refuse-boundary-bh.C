// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// Near miss: a non-zero comparison boundary lowers arithmetically, not
// as the proven sign/zero CC test -- refuse by name, keep the CC form.
// { dg-final { scan-tree-dump "ccmask refused .ccmask-boundary-unsupported" "rvtt_ccmask" } }
// { dg-final { scan-assembler-not "SFPGT" } }
// { dg-final { scan-assembler "SFPMOV" } }
#define CCMASK_FN ccmask_boundary
#define CCMASK_COND(x) ((x) <= 1.0f)
#define CCMASK_X x
#define CCMASK_Y y
#define CCMASK_W w
#define CCMASK_A 0.4375f
#define CCMASK_B 1.5f
#include "ccmask-body.h"
