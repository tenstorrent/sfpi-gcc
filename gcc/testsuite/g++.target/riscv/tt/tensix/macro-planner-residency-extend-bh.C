// Check that descriptor residency extends past an enclosing block loop so the words are programmed once per kernel.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-planner-residency -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner prefix-epoch: cross-tile config invariance proven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner residency: descriptor program resident 1 enclosing level.s. further out" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable prefix-epoch=hoisted" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPENCC" 2 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "SFPLOADI" 8 } }

#include "macro-planner-residency-block-body.h"

BLOCK_KERNEL (select_blocks)
