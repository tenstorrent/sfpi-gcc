// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// Stage-B default-off control: the same multi-block layout as the
// stage-B fire twin, WITHOUT -mtt-tensix-optimize-cc-region-general.
// The stage-A machine refuses the scalar branch by its standing name
// and the CC lowering is untouched -- the stage-B flag is byte-inert
// when absent.
// { dg-final { scan-tree-dump "ccmask refused .ccmask-region-foreign-stmt" "rvtt_ccmask" } }
// { dg-final { scan-tree-dump-not "folded zeroing CC region" "rvtt_ccmask" } }
// { dg-final { scan-assembler "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPGT" } }
#define CCL_REGION_BODY(y, s, vp) \
  do { (y) = sfpi::vFloat (0.0f); if ((s) & 1) (void) *(vp); } while (0)
#define CCL_FN ccl_off
#define CCL_X x
#define CCL_Y y
#define CCL_S s
#define CCL_VP vp
#include "ccmask-layout-body.h"
