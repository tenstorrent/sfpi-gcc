// Track B near-miss: the first load executes under narrowed lane state
// (inside v_if), so its LREG holds proven data only in the enabled
// lanes; forwarding it to a later full-lane reload is unproved and the
// pair must refuse with the CC vocabulary word.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-ownership -fdump-rtl-rvtt_dst_ownership-details" }
// { dg-final { scan-rtl-dump "cc-enable-unproved" "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump-not "Dst-ownership fold" "rvtt_dst_ownership" } }
// { dg-final { scan-assembler-times {\mSFPLOAD\t} 2 } }

#include "dst-ownership-prologue.h"

using namespace sfpi;

__attribute__((noinline)) void
masked_first_load (float sel)
{
  vFloat g = vFloat (sel);
  vFloat first = vFloat (0.0f);
  v_if (g >= 1.0F) { first = dst_reg[0]; }	// masked-lane load
  v_endif;
  vFloat r = first * 2.0f;
  vFloat again = dst_reg[0];			// full-lane reload
  dst_reg[0] = r * again;
}
