// Track B near-miss: a configuration write between the load and the
// reload is a Dst-layout boundary (the layout is CFG state, not value
// state) and must kill the face's identity chain.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-ownership -fdump-rtl-rvtt_dst_ownership-details" }
// { dg-final { scan-rtl-dump "dst-rwc-effect-unproved .layout-boundary." "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump-not "Dst-ownership fold" "rvtt_dst_ownership" } }
// { dg-final { scan-assembler-times {\mSFPLOAD\t} 2 } }

#include "dst-ownership-prologue.h"

using namespace sfpi;

__attribute__((noinline)) void
config_write_between ()
{
  vFloat in = dst_reg[0];
  vFloat r = in * 0.75f;
  vConstFloatPrgm0 = 1.5f;		// SFPCONFIG destination write
  vFloat again = dst_reg[0];
  dst_reg[0] = r * again;
}
