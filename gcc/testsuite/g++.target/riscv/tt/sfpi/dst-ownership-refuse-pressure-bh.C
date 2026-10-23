// Check that the reload fold is refused when the longer live range would need a ninth LREG.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-ownership -fdump-rtl-rvtt_dst_ownership-details" }
// { dg-final { scan-rtl-dump "lreg-pressure-exceeded" "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump-not "Dst-ownership fold" "rvtt_dst_ownership" } }
// { dg-final { scan-assembler-times {\mSFPLOAD\t} 2 } }

#include "dst-ownership-prologue.h"

using namespace sfpi;

__attribute__((noinline)) void
pressure_reload ()
{
  dst_ownership_enable_all_lanes ();
  vFloat in = dst_reg[0];
  vFloat a = in * 1.5f;			// in dies here
  vFloat b = a + 1.0f;
  vFloat c = a + 2.0f;
  vFloat d = a + 3.0f;
  vFloat e = a + 4.0f;
  vFloat f = a + 5.0f;
  vFloat g = a + 6.0f;
  vFloat h = a + 7.0f;			// peak: a..h = 8 live
  vFloat s = ((a + b) + (c + d)) + ((e + f) + (g + h));
  vFloat in2 = dst_reg[0];		// extension would need a 9th LREG
  dst_reg[0] = s * in2;
}
