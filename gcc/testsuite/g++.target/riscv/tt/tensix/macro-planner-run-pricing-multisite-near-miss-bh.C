// Check that a callee with two call sites gets no caller-loop amortization and keeps the per-run pricing refusal.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner init-hoist-refusal: drain-init-callers-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formation-refusal: unprofitable" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner run-pricing: init-hoist-amortized" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed:" "rvtt_macro_planner" } }
// { dg-final { scan-assembler "SFPSWAP" } }
// { dg-final { scan-assembler "TTINCRWC" } }

#define RP_SECOND_CALLER caller_tiles_second
#include "run-pricing-minmax-markerless-body.h"
