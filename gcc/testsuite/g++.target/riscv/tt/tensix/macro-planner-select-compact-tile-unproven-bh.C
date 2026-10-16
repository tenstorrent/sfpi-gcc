// Check that a runtime word pushed to MMIO refuses prefix-epoch-unproven and keeps the prefix per tile.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner prefix-epoch-refusal: prefix-epoch-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "prefix-epoch=hoisted" "rvtt_macro_planner" } }

#define TILE_EXTRA_BOILERPLATE() *sync_word = tiles
#include "macro-planner-tile-hoist-body.h"

TILE_KERNEL (select_tiles_unproven_word)
