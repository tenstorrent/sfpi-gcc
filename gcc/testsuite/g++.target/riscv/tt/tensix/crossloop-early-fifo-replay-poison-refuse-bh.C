// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_crossloop" }
// { dg-final { scan-tree-dump-not "hoisted across loop" "rvtt_crossloop" } }
// { dg-final { scan-tree-dump "crossloop-cc-ambient-unproven" "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-not "proven ALL-LANES" "rvtt_crossloop" } }
#include "crossloop-hoist-ccgeneral-allanes-fire-bh.C"

// A different public entry can begin capture; local resets cannot erase it.
void independent_public_recorder ()
{
  *(volatile unsigned *)0xffe40000u = 0x04020001u;
}
