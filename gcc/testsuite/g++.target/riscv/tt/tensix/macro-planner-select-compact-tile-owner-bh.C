// Near-miss: one raw SFPCONFIG word to a planner-owned destination in
// the per-tile boilerplate is an intervening owner -- the epoch refuses
// by name and the prefix stays per tile, byte-identically to the
// pre-elision compiler.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner prefix-epoch-refusal: prefix-epoch-invalidated" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "prefix-epoch=hoisted" "rvtt_macro_planner" } }

#define TILE_EXTRA_BOILERPLATE()                                              \
  __asm__ __volatile__ (".ttinsn %0" :: "n" (0x91000040))
#include "macro-planner-tile-hoist-body.h"

TILE_KERNEL (select_tiles_foreign_owner)
