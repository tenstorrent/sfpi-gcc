// Track B: a reload consumed under predication still folds -- the
// compiler lowers a v_if-scoped `x = dst_reg[0]' as a full-lane load
// into a fresh temporary followed by a lane-predicated assign (the lv
// merge lives on the assign, not the load), so replacing the load with
// the first load's LREG-resident value is exact.  (A load that itself
// carried a live-value merge operand would instead refuse
// live-value-merge; the backend never emits that shape here.)
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-ownership -fdump-rtl-rvtt_dst_ownership-details" }
// { dg-final { scan-rtl-dump-times "Dst-ownership fold: reload insn" 1 "rvtt_dst_ownership" } }
// { dg-final { scan-assembler-times {\mSFPLOAD\t} 1 } }

#include "dst-ownership-prologue.h"

using namespace sfpi;

__attribute__((noinline)) void
merged_reload ()
{
  vFloat in = dst_reg[0];
  vFloat r = in * 8.0f;
  vFloat sel = vFloat (2.0f);
  v_if (r >= 4.0F) { sel = dst_reg[0]; }	// predicated consumer
  v_endif;
  dst_reg[0] = r * sel;
}
