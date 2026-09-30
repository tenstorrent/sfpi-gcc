// Cross-tile prefix elision: the compact select face loop nested
// in a guarded tile loop full of LLK-shaped boilerplate (raw SETC16 /
// LaneConfig-reset / sync words plus a dynamic partially-constant MMIO
// push).  The configuration-epoch proof shows no intervening owner of
// the planner's SFPCONFIG destinations across the tile loop, so the 13
// descriptor words hoist to the split entry edge and are elided from
// every tile after the first; the ambient enable and the owned SETC16
// program stay per tile (recurring prefix 17 -> 4 words).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: cc-compact" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner prefix-epoch: cross-tile config invariance proven .owned SFPCONFIG dests epoch-clean across the enclosing loop; 13 descriptor words hoisted to the outer preheader; enable.setc16 retained per trip." "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable prefix-epoch=hoisted" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPENCC" 2 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "SFPLOADI" 8 } }

#include "macro-planner-tile-hoist-body.h"

TILE_KERNEL (select_tiles)
