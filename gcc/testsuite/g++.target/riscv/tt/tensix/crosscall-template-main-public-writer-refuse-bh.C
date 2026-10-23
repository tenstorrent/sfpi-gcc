// Main does not make an uncalled public arbitrary-pointer writer dead.
// An external entry may pass the MOP template aperture as its argument.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosscall-hoist -fdump-tree-rvtt_crosscall" }
// { dg-final { scan-tree-dump "mop-template-alias-unproven" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump-not "hoisted 6 contract materializations" "rvtt_crosscall" } }

#include "crosscall-hoist-scan-entry-offcontract-bh.C"

__attribute__((noinline)) void public_pointer_writer (volatile unsigned *p,
						     unsigned word)
{
  *p = word;
}
