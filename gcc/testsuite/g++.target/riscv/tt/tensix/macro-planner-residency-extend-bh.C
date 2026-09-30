// Descriptor residency, outward extension FIRE: the cross-tile prefix-elision shape
// nested in one further enclosing guarded block loop.  Prefix elision alone
// hoists the 13 descriptor words to the tile loop's entry -- inside
// the block loop, re-pushed once per block; the residency extension
// re-proves the epoch discipline over the block loop's body (guarded
// by the function-wide owned-state invariance walk that discharges
// skip-path occurrence) and the words become resident once per kernel.
// Word counts are unchanged versus prefix elision alone (no duplication; the fire is
// placement, dump-pinned).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-planner-residency -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner prefix-epoch: cross-tile config invariance proven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner residency: descriptor program resident 1 enclosing level.s. further out" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable prefix-epoch=hoisted" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPENCC" 2 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "SFPLOADI" 8 } }

#include "macro-planner-residency-block-body.h"

BLOCK_KERNEL (select_blocks)
