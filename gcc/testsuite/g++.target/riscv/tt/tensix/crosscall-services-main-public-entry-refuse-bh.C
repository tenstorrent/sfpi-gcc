// A main anchor cannot authorize deleting a public service's init prefix.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "init-hoist: closure .callee-entry-unproved." "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "init contract hoisted to caller" "rvtt_macro_planner" } }
// { dg-final { scan-assembler "SFPCONFIG" } }

#include "crosscall-services-public-entry-refuse-bh.C"

int main ()
{
  public_entry_caller (2);
  return 0;
}
