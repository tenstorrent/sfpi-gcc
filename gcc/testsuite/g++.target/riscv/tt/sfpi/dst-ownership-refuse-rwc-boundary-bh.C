// Check that a dst_reg++ between load and reload blocks the fold (rwc-boundary).
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-ownership -fdump-rtl-rvtt_dst_ownership-details" }
// { dg-final { scan-rtl-dump "dst-rwc-effect-unproved .rwc-boundary." "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump-not "Dst-ownership fold" "rvtt_dst_ownership" } }
// { dg-final { scan-assembler-times {\mSFPLOAD\t} 2 } }

#include "dst-ownership-prologue.h"

using namespace sfpi;

__attribute__((noinline)) void
rwc_boundary_between ()
{
  dst_ownership_enable_all_lanes ();
  vFloat in = dst_reg[0];
  vFloat r = in * 0.5f + 0.25f;
  dst_reg++;				// RWC boundary: base moved
  vFloat other = dst_reg[0];		// same tuple, different rows
  dst_reg[0] = r * other;
}
