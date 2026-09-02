// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_ccmask" }
// THE STAGE-B LAYOUT FIRE (the refusal registry4 stage B): the
// zeroing frame carries scalar control flow AFTER the predicated
// assign, so its statements span blocks and the stage-A linear
// machine refuses the layout (ccmask-region-foreign-stmt on the
// scalar branch) -- while the CC-region tree proves the identical
// frame structure (chain [xvif,fcmps,condb], single exit, one zeroing
// assign dominating it) and the tree-keyed matcher folds it.
// { dg-final { scan-tree-dump "ccmask: tree-keyed layout admission" "rvtt_ccmask" } }
// { dg-final { scan-tree-dump-times "ccmask: folded zeroing CC region" 1 "rvtt_ccmask" } }
// { dg-final { scan-assembler-times "SFPGT" 1 } }
// { dg-final { scan-assembler "SFPAND" } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
#define CCL_REGION_BODY(y, s, vp) \
  do { (y) = sfpi::vFloat (0.0f); if ((s) & 1) (void) *(vp); } while (0)
#define CCL_FN ccl_fire
#define CCL_X x
#define CCL_Y y
#define CCL_S s
#define CCL_VP vp
#include "ccmask-layout-body.h"
