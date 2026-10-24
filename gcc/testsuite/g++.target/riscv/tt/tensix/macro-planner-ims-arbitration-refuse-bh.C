#include "rtl-lane-reset.h"

// Check that formation refuses when the RISC-pushed calendar prices above the replay-delivered explicit rows.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-dst-autoincr -mtt-tensix-macro-ims -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner ims-arbitration: formed=15206 replay-alt=14400 .centislots; run-rows=8 ii=12 row-words=18. -> replay-delivery-preferred" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formation-refusal: replay-delivery-preferred" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed:" "rvtt_macro_planner" } }

#include "macro-planner-ims-intmul-row.h"
