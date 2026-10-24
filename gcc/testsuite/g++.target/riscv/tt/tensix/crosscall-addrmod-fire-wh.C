// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-crosscall-addrmod -fdump-rtl-rvtt_dst_autoincr-details" }
// Check the ADDR_MOD contract on WH: scratch modifier 2 is physical slot 6
// (registers 19/29/54) under the base-1 contract.  Replay is disabled to keep
// the explicit-row shape.
// { dg-final { scan-rtl-dump "addrmod-hoist: placed ADDR_MOD contract .3 setc16." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "Dst-autoincr crosscall-addrmod contract: rows 8 stride 2" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr refusal:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTSETC16\t19, 0" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t29, 2" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t54, 0" 1 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 2" 8 } }

#define CAM_MODE 3
#define CAM_STRIDE 2
#define CAM_ROWS 8
#include "crosscall-addrmod-body.h"
