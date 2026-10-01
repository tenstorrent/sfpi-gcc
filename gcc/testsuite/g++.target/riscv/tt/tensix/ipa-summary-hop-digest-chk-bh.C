// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-early-inlining -fchecking=2 -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-crosscall-addrmod -fdump-rtl-rvtt_dst_autoincr-details" }
// Item #15 assertion phase: the same hop-digest shape under
// -fchecking, where the legacy per-statement hop walk shadows the
// digest replay and any verdict divergence is a hard assert.  A clean
// compile with the contract placed IS the agreement proof.
// { dg-final { scan-rtl-dump "addrmod-hoist: placed ADDR_MOD contract .3 setc16." "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }

#define CAM_MODE 7
#define CAM_STRIDE 2
#define CAM_ROWS 8
#define CAM_NO_CALLER
#include "crosscall-addrmod-body.h"

static void wrap () { cam_callee (); }

void
cam_caller (int tiles)
{
  for (int t = 0; t != tiles; ++t)
    wrap ();
}
