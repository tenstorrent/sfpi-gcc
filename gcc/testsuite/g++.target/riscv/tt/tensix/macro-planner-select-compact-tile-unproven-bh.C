#include "rtl-lane-reset.h"

// A runtime MMIO word cannot establish physical reset delivery or license a
// cross-tile prefix hoist. The physical-lane gate may refuse first.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump {Macro-planner (prefix-epoch-refusal: prefix-epoch-unproven|formation-refusal: cc-enable-unproved)} "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "prefix-epoch=hoisted" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed:" "rvtt_macro_planner" } }
// Retain both the unknown runtime store and the bounded thread-config store.
// { dg-final { scan-assembler-times {sw\t[^\n]*,0\([^)]*\)} 2 } }

#define TILE_EXTRA_BOILERPLATE() *sync_word = tiles
#include "macro-planner-tile-hoist-body.h"

TILE_KERNEL (select_tiles_unproven_word)
