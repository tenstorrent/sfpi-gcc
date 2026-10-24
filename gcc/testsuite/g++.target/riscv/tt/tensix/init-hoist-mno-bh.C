#include "rtl-lane-reset.h"

// Check that -mno-tt-tensix-optimize-init-hoist keeps the per-call init prefix.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mno-tt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-not "Macro-planner init-hoist" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 7 } }
// Two physical lane resets plus the retained planner-owned enable.
// { dg-final { scan-assembler-times "SFPENCC\\t3, 10" 3 } }

#include "init-hoist-caller-body.h"
