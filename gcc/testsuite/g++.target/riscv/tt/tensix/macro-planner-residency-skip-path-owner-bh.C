// WP13 residency outward extension, near miss: a foreign raw SFPCONFIG
// word to an owned destination AFTER the block loop.  Neither WP11's
// tile-loop proof nor the block-loop walk would see it, but the
// resident program's occurrence set grows on paths whose owned-state
// future this word redefines -- the function-wide owned-state
// invariance walk refuses (resid-skip-path-unproven) and the placement
// stays WP11's byte-identically.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-planner-residency -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner prefix-epoch: cross-tile config invariance proven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner residency-refusal: resid-skip-path-unproven .prefix-epoch-invalidated" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner residency: descriptor program resident" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable prefix-epoch=hoisted" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }

#define BLOCK_POST()                                                          \
  __asm__ __volatile__ (".ttinsn %0" :: "n" (0x91000040))
#include "macro-planner-residency-block-body.h"

BLOCK_KERNEL (skip_path_blocked_select_kernel)
