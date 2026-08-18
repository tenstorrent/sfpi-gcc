// Replay-arm twin of macro-planner-ims-arbitration-refuse-bh.C: with
// -mtt-tensix-macro-planner-replay the formed launches ride the replay
// unit themselves, delivery hides under execution, and the same model
// prices formation BELOW the alternative:
// 123*26 config + 8*12*100 launches + 200 drain = 12998 < 14400.
// Identical source, identical shape -- only the delivery arm differs,
// and the arbitration decision follows the number.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-dst-autoincr -mtt-tensix-macro-planner-replay -mtt-tensix-macro-ims -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner ims-arbitration: formed=12998 replay-alt=14400 .centislots; run-rows=8 ii=12 row-words=18. -> form" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "replay-delivery-preferred" "rvtt_macro_planner" } }

#include "macro-planner-ims-intmul-row.h"
