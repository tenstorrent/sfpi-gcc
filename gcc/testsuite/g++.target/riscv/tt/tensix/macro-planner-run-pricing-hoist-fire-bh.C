// Check that init-hoist-aware pricing charges the configuration prefix once per caller-loop entry and the callee forms.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner init-hoist pricing pre-run: stage=2 proven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner run-pricing: init-hoist-amortized config=\\d+ per-run=\\d+ explicit=\\d+ weight=\\d+/\\d+ -> profitable" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner init-hoist: stage=2 init contract hoisted to caller loop preheader" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=32 runs=4 lane-proof=materialized-enable init-hoist=full" "rvtt_macro_planner" } }
// The callee is pure payload: enable, SETC16 program, and descriptor
// words all live in the caller's preheader.
// { dg-final { scan-assembler-times "SFPENCC\\t3, 10" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\\t18, 0" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\\t34, 2" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\\t53, 0" 1 } }

#include "run-pricing-minmax-markerless-body.h"
