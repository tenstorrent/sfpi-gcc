// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// The mirrored direction: zeroing under `x > 0.0f` folds to the SFPLE
// keep-mask.
// { dg-final { scan-tree-dump-times "ccmask: folded zeroing CC region" 1 "rvtt_ccmask" } }
// { dg-final { scan-assembler-times "SFPLE\tL\[0-7\], L9, 0, 8" 1 } }
// { dg-final { scan-assembler "SFPAND" } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
#define CCMASK_FN ccmask_gtdir
#define CCMASK_COND(x) ((x) > 0.0f)
#define CCMASK_X x
#define CCMASK_Y y
#define CCMASK_W w
#define CCMASK_A 0.4375f
#define CCMASK_B 1.5f
#include "ccmask-body.h"
