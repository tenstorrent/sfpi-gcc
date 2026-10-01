// Track B near-miss: a Dst store between the load and the reload may
// alias the reloaded rows, and a store/reload pair is not proven
// bit-identity (the store's data-format round trip can be lossy).  The
// pair must NOT cancel.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-ownership -fdump-rtl-rvtt_dst_ownership-details" }
// { dg-final { scan-rtl-dump "dst-rwc-effect-unproved .dst-store-may-alias." "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump-not "Dst-ownership fold" "rvtt_dst_ownership" } }
// { dg-final { scan-assembler-times {\mSFPLOAD\t} 2 } }

#include "dst-ownership-prologue.h"

using namespace sfpi;

__attribute__((noinline)) void
store_then_reload ()
{
  vFloat in = dst_reg[0];
  vFloat r = in * 2.0f + 3.0f;
  dst_reg[0] = r;			// intervening Dst writer
  vFloat back = dst_reg[0];		// NOT the first load's value
  dst_reg[0] = back * in;
}
