// Varied adversary twin of macro-planner-run-pricing-hoist-fire-bh.C:
// the min arm (sfpselect2 index 0) instead of max -- the pricing and
// the hoist contract are payload-value-independent.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner init-hoist pricing pre-run: stage=2 proven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner run-pricing: init-hoist-amortized config=\\d+ per-run=\\d+ explicit=\\d+ weight=\\d+/\\d+ -> profitable" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=32 runs=4 lane-proof=materialized-enable init-hoist=full" "rvtt_macro_planner" } }

#define RP_ROW_RESULT 0
#define RP_CALLEE_NAME periodic_min_markerless
#define RP_CALLER_NAME caller_tiles_min
#include "run-pricing-minmax-markerless-body.h"
