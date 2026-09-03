// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_ccmask" }
// Control twin of the EQ/NE priced refusal: the WHEN-gate prices only
// the two-compare EQ/NE compositions.  Under the SAME flag set, the
// order-direction fold (`x > 0.0f` -> single SFPLE keep-mask, two
// delivered words for the skeleton's five) still prices profitable
// and fires exactly as without -mtt-tensix-optimize-cc-region-general
// (ccmask-zero-gtdir-bh.C).
// { dg-final { scan-tree-dump-times "ccmask: folded zeroing CC region" 1 "rvtt_ccmask" } }
// { dg-final { scan-tree-dump-not "ccmask-eqne-fold-unprofitable" "rvtt_ccmask" } }
// { dg-final { scan-assembler-times "SFPLE\tL\[0-7\], L9, 0, 8" 1 } }
// { dg-final { scan-assembler "SFPAND" } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
#define CCMASK_FN ccmask_gtdir_general
#define CCMASK_COND(x) ((x) > 0.0f)
#define CCMASK_X x
#define CCMASK_Y y
#define CCMASK_W w
#define CCMASK_A 0.4375f
#define CCMASK_B 1.5f
#include "ccmask-body.h"
