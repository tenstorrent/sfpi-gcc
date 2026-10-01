// NEAR-MISS (the mandated one): the SAME marker-free shape, but the
// callee has TWO call sites -- the init-hoist closure refuses
// (multi-site), no amortization license exists, the frozen
// conservative-per-run pricing holds, and the production refusal
// RETURNS.  Bytes keep the explicit rows.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner init-hoist-refusal: drain-init-callers-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formation-refusal: unprofitable" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner run-pricing: init-hoist-amortized" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed:" "rvtt_macro_planner" } }
// { dg-final { scan-assembler "SFPSWAP" } }
// { dg-final { scan-assembler "TTINCRWC" } }

#define RP_SECOND_CALLER caller_tiles_second
#include "run-pricing-minmax-markerless-body.h"
