// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_ccmask" }
// Check that a nested frame inside the zeroing frame is refused (ccmask-region-shape).
// { dg-final { scan-tree-dump "ccmask refused .ccmask-region-shape" "rvtt_ccmask" } }
// { dg-final { scan-tree-dump-not "folded zeroing CC region" "rvtt_ccmask" } }
// { dg-final { scan-assembler "SFPLE" } }
// { dg-final { scan-assembler "SFPENCC" } }
// { dg-final { scan-assembler-not "SFPGT\tL\[0-7\], L9" } }
#define CCL_REGION_BODY(y, s, vp) \
  do { \
    (y) = sfpi::vFloat (0.0f); \
    v_if ((y) >= -1.0f) { (y) = (y) + sfpi::vFloat (1.5f); } v_endif; \
  } while (0)
#define CCL_FN ccl_nested
#define CCL_X x
#define CCL_Y y
#define CCL_S s
#define CCL_VP vp
#include "ccmask-layout-body.h"
