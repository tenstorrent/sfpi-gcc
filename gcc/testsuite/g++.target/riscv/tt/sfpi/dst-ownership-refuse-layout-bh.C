// Check that a config write between load and reload blocks the fold (layout-boundary).
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-ownership -fdump-rtl-rvtt_dst_ownership-details" }
// { dg-final { scan-rtl-dump "dst-rwc-effect-unproved .layout-boundary." "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump-not "Dst-ownership fold" "rvtt_dst_ownership" } }
// { dg-final { scan-assembler-times {\mSFPLOAD\t} 2 } }

#include "dst-ownership-prologue.h"

using namespace sfpi;

__attribute__((noinline)) void
config_write_between ()
{
  dst_ownership_enable_all_lanes ();
  vFloat in = dst_reg[0];
  vFloat r = in * 0.75f;
  vConstFloatPrgm0 = 1.5f;		// SFPCONFIG destination write
  vFloat again = dst_reg[0];
  dst_reg[0] = r * again;
}
