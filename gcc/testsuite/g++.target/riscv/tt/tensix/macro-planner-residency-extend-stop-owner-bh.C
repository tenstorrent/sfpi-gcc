#include "rtl-lane-reset.h"

// Check that a foreign CC write at block level stops the residency extension at the tile-loop placement.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-planner-residency -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner prefix-epoch-refusal: prefix-epoch-invalidated" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner residency: descriptor program resident" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "prefix-epoch=hoisted" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 7 } }

#define BLOCK_PRE()                                                           \
  __builtin_rvtt_sfpencc (10, 0)
// Preserve the real foreign partial-CC spoiler at block scope, but execute
// LaneConfig resets with proved full flags at the actual tile placement.
#define TILE_LANE_PROLOGUE() RVTT_TEST_LANE_RESET ()
#include "macro-planner-residency-block-body.h"

BLOCK_KERNEL (cc_blocked_select_kernel)
