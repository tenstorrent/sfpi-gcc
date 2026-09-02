// Residency outward extension, near miss: a foreign pure-CC write
// (lanes-off SFPENCC -- no owned-destination effect, so the
// function-wide owned-state invariance walk admits it) at BLOCK level,
// inside the block loop but outside the tile loop.  Prefix elision's tile-loop
// proof stays clean and fires; the extension's block-loop walk keeps
// prefix elision's full conservative discipline (foreign CC dataflow refuses)
// and STOPS at the prefix-elision placement byte-identically: no residency fire,
// same word counts.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-planner-residency -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner prefix-epoch: cross-tile config invariance proven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner residency-extension-stop: prefix-epoch-invalidated" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner residency: descriptor program resident" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable prefix-epoch=hoisted" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }

#define BLOCK_PRE()                                                           \
  __builtin_rvtt_sfpencc (0, 10)
#include "macro-planner-residency-block-body.h"

BLOCK_KERNEL (cc_blocked_select_kernel)
