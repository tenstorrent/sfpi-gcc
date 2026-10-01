// Near miss (closure): a second call site leaves the per-call prefix
// in place -- refused by name, everything byte-identical to the
// unhoisted form.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner init-hoist-refusal: drain-init-callers-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "init contract hoisted" "rvtt_macro_planner" } }

#include "init-hoist-caller-body.h"

__attribute__((noinline)) void second_site ()
{
  periodic_minmax_inplace ();
}
