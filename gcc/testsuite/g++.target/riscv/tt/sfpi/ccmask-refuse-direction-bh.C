// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// Near miss: `x < 0.0f` -- its complement (x >= 0) keeps +0, which the
// single-order keep-masks cannot express with the constant register on
// the read-only side; refuse by name.
// { dg-final { scan-tree-dump "ccmask refused .ccmask-compare-direction-unsupported" "rvtt_ccmask" } }
// { dg-final { scan-assembler-not "SFPGT" } }
// { dg-final { scan-assembler-not "SFPLE" } }
#define CCMASK_FN ccmask_ltdir
#define CCMASK_COND(x) ((x) < 0.0f)
#define CCMASK_X x
#define CCMASK_Y y
#define CCMASK_W w
#define CCMASK_A 0.4375f
#define CCMASK_B 1.5f
#include "ccmask-body.h"
