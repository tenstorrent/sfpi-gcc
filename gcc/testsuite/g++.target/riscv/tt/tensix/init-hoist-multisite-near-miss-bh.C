#include "rtl-lane-reset.h"

// Check that a callee with a second call site keeps its per-call init prefix.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner init-hoist-refusal: drain-init-callers-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "init contract hoisted" "rvtt_macro_planner" } }

#include "init-hoist-caller-body.h"

__attribute__((noinline)) void second_site ()
{
  RVTT_TEST_LANE_RESET ();
  periodic_minmax_inplace ();
}
