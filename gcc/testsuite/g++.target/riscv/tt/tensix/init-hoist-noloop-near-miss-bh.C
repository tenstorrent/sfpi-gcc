// The public callee can also be called from outside this TU, so its
// visible straight-line caller is not a complete call-site census.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "init-hoist: closure \\(callee-external-entry\\)" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "init contract hoisted" "rvtt_macro_planner" } }

#define MINMAX_FOUR_FACE_RUNS 1
#include "loadmacro-periodic-minmax-inplace-body.h"

__attribute__((noinline)) void straightline_caller ()
{
  periodic_minmax_inplace ();
}
