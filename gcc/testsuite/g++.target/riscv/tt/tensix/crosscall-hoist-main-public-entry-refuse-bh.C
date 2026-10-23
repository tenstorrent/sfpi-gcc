// A main anchor does not hide a public callee from external ABI entries.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -fdump-tree-rvtt_crosscall" }
// { dg-final { scan-tree-dump "crosscall-callee-entry-unproved" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump-not "hoisted 6 contract materializations" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump-not "contract read" "rvtt_crosscall" } }

#include "crosscall-hoist-public-entry-refuse-bh.C"

int main ()
{
  initialized_private_caller (2);
  return 0;
}
