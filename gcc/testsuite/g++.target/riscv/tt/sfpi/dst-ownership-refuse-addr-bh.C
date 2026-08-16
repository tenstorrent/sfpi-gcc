// Track B near-miss: a reload of a DIFFERENT typed Dst address is not a
// candidate at all -- nothing may fold and no identity is claimed.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-ownership -fdump-rtl-rvtt_dst_ownership-details" }
// { dg-final { scan-rtl-dump-not "Dst-ownership fold" "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump "0 reload.s. folded" "rvtt_dst_ownership" } }
// { dg-final { scan-assembler-times {\mSFPLOAD\t} 2 } }

#include "dst-ownership-prologue.h"

using namespace sfpi;

__attribute__((noinline)) void
different_address ()
{
  vFloat a = dst_reg[0];
  vFloat r = a * 4.0f + 5.0f;
  vFloat b = dst_reg[2];		// different logical address
  dst_reg[0] = r * b;
}
