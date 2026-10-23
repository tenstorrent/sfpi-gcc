#include "rtl-lane-reset.h"

// Check that with -mtt-tensix-optimize-epoch-metadata the epoch walk steps over a no-LREG marker and the words hoist.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-epoch-metadata -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner prefix-epoch: cross-tile config invariance proven .owned SFPCONFIG dests epoch-clean across the enclosing loop; 13 descriptor words hoisted to the outer preheader; enable.setc16 retained per trip." "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable prefix-epoch=hoisted" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPENCC" 3 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 6 } }
// { dg-final { scan-assembler-times "SFPLOADI" 8 } }

#define TILE_EXTRA_BOILERPLATE() __builtin_rvtt_sfprawlreg_effect (0, 0)
#include "macro-planner-tile-hoist-body.h"

TILE_KERNEL (select_tiles)
