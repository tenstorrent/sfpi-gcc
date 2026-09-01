// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_ccmask" }
// Stage-B near miss: the zeroing assign sits under a SCALAR branch
// inside the frame, so it does not execute on every region execution
// -- the tree-keyed matcher's execution-order proof (assign dominates
// the exit popc) fails and the candidate refuses BY ITS OWN NAME; the
// CC lowering is untouched.
// { dg-final { scan-tree-dump "ccmask refused .ccmask-region-layout-unproven" "rvtt_ccmask" } }
// { dg-final { scan-tree-dump-not "folded zeroing CC region" "rvtt_ccmask" } }
// { dg-final { scan-assembler "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPGT" } }
#define CCL_REGION_BODY(y, s, vp) \
  do { if ((s) & 1) (y) = sfpi::vFloat (0.0f); } while (0)
#define CCL_FN ccl_conditional
#define CCL_X x
#define CCL_Y y
#define CCL_S s
#define CCL_VP vp
#include "ccmask-layout-body.h"
