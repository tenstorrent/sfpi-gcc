// Check that a Dst store between load and reload blocks the fold (dst-store-may-alias).
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-ownership -fdump-rtl-rvtt_dst_ownership-details" }
// { dg-final { scan-rtl-dump "dst-rwc-effect-unproved .dst-store-may-alias." "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump-not "Dst-ownership fold" "rvtt_dst_ownership" } }
// { dg-final { scan-assembler-times {\mSFPLOAD\t} 2 } }

#include "dst-ownership-prologue.h"

using namespace sfpi;

__attribute__((noinline)) void
store_then_reload ()
{
  dst_ownership_enable_all_lanes ();
  vFloat in = dst_reg[0];
  vFloat r = in * 2.0f + 3.0f;
  dst_reg[0] = r;			// intervening Dst writer
  vFloat back = dst_reg[0];		// NOT the first load's value
  dst_reg[0] = back * in;
}
