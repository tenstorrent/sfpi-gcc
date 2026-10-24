#include "rtl-lane-reset.h"

// Check that without -mtt-tensix-optimize-epoch-metadata a raw-LREG effect marker keeps the descriptor words per tile.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner prefix-epoch-refusal: prefix-epoch-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "prefix-epoch=hoisted" "rvtt_macro_planner" } }

#define TILE_EXTRA_BOILERPLATE() __builtin_rvtt_sfprawlreg_effect (0, 0)
#include "macro-planner-tile-hoist-body.h"

TILE_KERNEL (select_tiles)
