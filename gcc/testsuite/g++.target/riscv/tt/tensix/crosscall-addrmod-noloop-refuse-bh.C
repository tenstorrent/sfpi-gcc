// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-crosscall-addrmod -fdump-rtl-rvtt_dst_autoincr-details" }
// Check that a call site outside any loop refuses the contract by name.
// { dg-final { scan-rtl-dump "crosscall-addrmod-unproven .crosscall-addrmod-loop-unproven." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "addrmod-hoist: placed" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 8 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

#define CAM_MODE 7
#define CAM_STRIDE 2
#define CAM_ROWS 8
#define CAM_NO_CALLER
#include "crosscall-addrmod-body.h"

void
cam_caller_straight ()
{
  cam_callee ();
}
