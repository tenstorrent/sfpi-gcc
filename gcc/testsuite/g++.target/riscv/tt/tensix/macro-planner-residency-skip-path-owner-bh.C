#include "rtl-lane-reset.h"

// Check that a raw SFPCONFIG to an owned destination after the block loop refuses the extension (resid-skip-path-unproven).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-planner-residency -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner prefix-epoch: cross-tile config invariance proven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner residency-refusal: resid-skip-path-unproven .prefix-epoch-invalidated" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner residency: descriptor program resident" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8 runs=1 config=preheader lane-proof=materialized-enable prefix-epoch=hoisted" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 6 } }

#define BLOCK_POST()                                                          \
  __asm__ __volatile__ (".ttinsn %0" :: "n" (0x91000040))
#include "macro-planner-residency-block-body.h"

BLOCK_KERNEL (skip_path_blocked_select_kernel)
