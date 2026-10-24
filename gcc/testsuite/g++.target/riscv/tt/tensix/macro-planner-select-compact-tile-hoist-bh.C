#include "rtl-lane-reset.h"

// Check that the compact select's descriptor words hoist out of a guarded tile loop with LLK-style boilerplate.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: cc-compact" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner prefix-epoch: cross-tile config invariance proven .owned SFPCONFIG dests epoch-clean across the enclosing loop; 13 descriptor words hoisted to the outer preheader; enable.setc16 retained per trip." "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable prefix-epoch=hoisted" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPENCC" 3 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 6 } }
// { dg-final { scan-assembler-times "SFPLOADI" 8 } }

#include "macro-planner-tile-hoist-body.h"

TILE_KERNEL (select_tiles)
