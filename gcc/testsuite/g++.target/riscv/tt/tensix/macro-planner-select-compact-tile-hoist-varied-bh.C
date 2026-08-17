// Varied-constant equivalent: different payload Dst addresses re-derive
// the same epoch proof (nothing is keyed on the reference constants).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner prefix-epoch: cross-tile config invariance proven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable prefix-epoch=hoisted" "rvtt_macro_planner" } }

#define TILE_TRUE_ADDR 96
#define TILE_FALSE_ADDR 128
#include "macro-planner-tile-hoist-body.h"

TILE_KERNEL (select_tiles_varied)
