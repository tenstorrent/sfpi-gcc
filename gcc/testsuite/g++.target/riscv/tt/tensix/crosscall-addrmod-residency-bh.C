// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-crosscall-addrmod -fdump-rtl-rvtt_dst_autoincr-details" }
// Residency walk FIRE: the call loop sits inside a scalar batch loop
// whose extra body passes the same epoch scan -- the program lifts to
// the BATCH loop's entry, the hand init discipline (lane HC's walk on
// the ADDR_MOD face).
// { dg-final { scan-rtl-dump "addrmod-hoist: placement lifted to enclosing loop" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "lifted 1 levels" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "Dst-autoincr crosscall-addrmod contract: rows 8 stride 2" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#define CAM_MODE 7
#define CAM_STRIDE 2
#define CAM_ROWS 8
#define CAM_NO_CALLER
#include "crosscall-addrmod-body.h"

int cam_pace;

void
cam_caller (int batches, int tiles)
{
  for (int b = 0; b != batches; ++b)
    {
      cam_pace = b;			/* scalar batch bookkeeping */
      for (int t = 0; t != tiles; ++t)
	cam_callee ();
    }
}
