// Formation-vs-replay arbitration: the derived intmul row forms under
// the established gates, but with the Dst auto-increment pass enabled
// (the replay-compressed alternative exists: the separator is absorbed
// around replay launches) and RISC-pushed launch delivery
// (no -mtt-tensix-macro-planner-replay), the shared cost model prices
// the formed calendar ABOVE the replay-delivered explicit rows:
// 123*(26 config + 8*12 launches) + 2*100 drain = 15206 >=
// 8 rows * 18 replayed words * 100 = 14400.  Formation refuses by name;
// both prices are dump API.  The companion replay-arm twin proves the
// decision keys on the delivery arm, never the shape.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-dst-autoincr -mtt-tensix-macro-ims -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner ims-arbitration: formed=15206 replay-alt=14400 .centislots; run-rows=8 ii=12 row-words=18. -> replay-delivery-preferred" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formation-refusal: replay-delivery-preferred" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed:" "rvtt_macro_planner" } }

#include "macro-planner-ims-intmul-row.h"
