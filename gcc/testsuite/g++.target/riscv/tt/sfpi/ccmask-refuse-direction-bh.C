// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// Genuine near miss: `x == 0.0f` -- EQ/NE are not order tests and have
// no single-order complement, so no SFPGT/SFPLE keep-mask expresses
// the kept set (which here splits both zeros from everything else);
// refuse by name.  All four order directions fold; equality never
// does.
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
