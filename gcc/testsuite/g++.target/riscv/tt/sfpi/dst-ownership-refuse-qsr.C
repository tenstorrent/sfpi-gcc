// Track B: QSR's RWC model (unified value field; untested simulator
// path) has no faithful typed representation -- the ownership pass
// hard-refuses the whole target, and both loads stay.
// { dg-options "-mcpu=tt-qsr32-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-ownership -fdump-rtl-rvtt_dst_ownership-details" }
// { dg-final { scan-rtl-dump "dst-rwc-effect-unproved .qsr-unmodeled." "rvtt_dst_ownership" } }
// { dg-final { scan-rtl-dump-not "Dst-ownership fold" "rvtt_dst_ownership" } }
// { dg-final { scan-assembler-times {\mSFPLOAD\t} 2 } }

#include "dst-ownership-prologue.h"

using namespace sfpi;

__attribute__((noinline)) void
qsr_reload ()
{
  vFloat in = dst_reg[0];
  vFloat r = in * 0.5f + 0.25f;
  vFloat again = dst_reg[0];
  dst_reg[0] = r * again;
}
