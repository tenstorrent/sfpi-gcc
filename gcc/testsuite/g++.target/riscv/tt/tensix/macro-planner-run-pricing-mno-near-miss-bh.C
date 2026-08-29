// NEAR-MISS (flag off): the same marker-free shape with
// -mno-tt-tensix-optimize-init-hoist -- no pricing pre-run exists, the
// frozen conservative-per-run pricing holds, and the refusal returns.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mno-tt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner formation-refusal: unprofitable" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner init-hoist pricing pre-run" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner run-pricing: init-hoist-amortized" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed:" "rvtt_macro_planner" } }
// { dg-final { scan-assembler "SFPSWAP" } }

#include "run-pricing-minmax-markerless-body.h"
