// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// Check that a multi-block zeroing frame is refused without -mtt-tensix-optimize-cc-region-general.
// { dg-final { scan-tree-dump "ccmask refused .ccmask-region-foreign-stmt" "rvtt_ccmask" } }
// { dg-final { scan-tree-dump-not "folded zeroing CC region" "rvtt_ccmask" } }
// { dg-final { scan-assembler "SFPLE" } }
// { dg-final { scan-assembler "SFPENCC" } }
// { dg-final { scan-assembler-not "SFPGT" } }
#define CCL_REGION_BODY(y, s, vp) \
  do { (y) = sfpi::vFloat (0.0f); if ((s) & 1) (void) *(vp); } while (0)
#define CCL_FN ccl_off
#define CCL_X x
#define CCL_Y y
#define CCL_S s
#define CCL_VP vp
#include "ccmask-layout-body.h"
