// Check that a foreign CC write at block level stops the residency extension at the tile-loop placement.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-planner-residency -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner prefix-epoch: cross-tile config invariance proven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner residency-extension-stop: prefix-epoch-invalidated" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner residency: descriptor program resident" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable prefix-epoch=hoisted" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }

#define BLOCK_PRE()                                                           \
  __builtin_rvtt_sfpencc (10, 0)
#include "macro-planner-residency-block-body.h"

BLOCK_KERNEL (cc_blocked_select_kernel)
