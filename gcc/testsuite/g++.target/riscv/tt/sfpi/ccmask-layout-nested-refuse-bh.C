// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_ccmask" }
// Stage-B near miss: a NESTED frame inside the zeroing frame -- the
// region is not a single zeroing assignment under one refinement
// chain, in any layout.  Both the stage-A machine and the tree-keyed
// matcher refuse by the standing shape name; the CC lowering is
// untouched.
// { dg-final { scan-tree-dump "ccmask refused .ccmask-region-shape" "rvtt_ccmask" } }
// { dg-final { scan-tree-dump-not "folded zeroing CC region" "rvtt_ccmask" } }
// { dg-final { scan-assembler "SFPSETCC" } }
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
