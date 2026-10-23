// Check that a Dst reload consumed under v_if still folds, since the merge is on the assign.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-ownership -fdump-rtl-rvtt_dst_ownership-details" }
// { dg-final { scan-rtl-dump-times "Dst-ownership fold: reload insn" 1 "rvtt_dst_ownership" } }
// { dg-final { scan-assembler-times {\mSFPLOAD\t} 1 } }

#include "dst-ownership-prologue.h"

using namespace sfpi;

__attribute__((noinline)) void
merged_reload ()
{
  dst_ownership_enable_all_lanes ();
  vFloat in = dst_reg[0];
  vFloat r = in * 8.0f;
  vFloat sel = vFloat (2.0f);
  v_if (r >= 4.0F) { sel = dst_reg[0]; }	// predicated consumer
  v_endif;
  dst_reg[0] = r * sel;
}
