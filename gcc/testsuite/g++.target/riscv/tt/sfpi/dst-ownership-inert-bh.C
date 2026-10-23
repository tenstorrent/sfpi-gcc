// Check that -mno-tt-tensix-optimize-dst-ownership reports the identity reload but keeps both loads.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mno-tt-tensix-optimize-dst-ownership -fdump-rtl-rvtt_dst_ownership-details" }
// { dg-final { scan-rtl-dump "provable identity reload" "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump "transform disabled" "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump-not "Dst-ownership fold" "rvtt_dst_ownership" } }
// { dg-final { scan-assembler-times {\mSFPLOAD\t} 2 } }

#include "dst-ownership-prologue.h"

using namespace sfpi;

__attribute__((noinline)) void
inert_reload ()
{
  dst_ownership_enable_all_lanes ();
  vFloat in = dst_reg[0];
  vFloat r = in * 0.30102999f + 1.25f;
  vFloat in2 = dst_reg[0];
  dst_reg[0] = copysgn (r, in2);
}
