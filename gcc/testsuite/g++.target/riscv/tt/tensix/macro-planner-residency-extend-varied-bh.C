// WP13 residency outward extension, varied-constants twin: different
// payload Dst addresses -- launch words change, the derived descriptor
// program and every proof are address-independent, the extension still
// fires.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-planner-residency -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner residency: descriptor program resident 1 enclosing level.s. further out" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable prefix-epoch=hoisted" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }

#define TILE_TRUE_ADDR 96
#define TILE_FALSE_ADDR 128
#include "macro-planner-residency-block-body.h"

BLOCK_KERNEL (varied_resident_select_kernel)
