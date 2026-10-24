#include "rtl-lane-reset.h"

// Check that init-hoist-aware run pricing fires identically under different names.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner init-hoist pricing pre-run: stage=2 proven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner run-pricing: init-hoist-amortized config=\\d+ per-run=\\d+ explicit=\\d+ weight=\\d+/\\d+ -> profitable" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=32 runs=4 lane-proof=materialized-enable init-hoist=full" "rvtt_macro_planner" } }

#define RP_CALLEE_NAME zq_wobble_kernel
#define RP_CALLER_NAME zq_wobble_driver
#include "run-pricing-minmax-markerless-body.h"
