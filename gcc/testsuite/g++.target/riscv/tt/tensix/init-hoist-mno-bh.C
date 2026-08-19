// Default-off twin: with -mno-tt-tensix-optimize-init-hoist nothing is
// attempted and the per-call prefix stays.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mno-tt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-not "Macro-planner init-hoist" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "SFPENCC\\t3, 10" 1 } }

#include "init-hoist-caller-body.h"
