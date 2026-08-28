// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-crosscall-addrmod -fdump-rtl-rvtt_dst_autoincr-details" }
// Call-boundary crossing boundary, firing side: one more row than the
// refusing twin pays the uncovered window and the contract fires.
// { dg-final { scan-rtl-dump "Dst-autoincr crosscall-addrmod contract: rows 7 stride 2" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#define CAM_MODE 7
#define CAM_STRIDE 2
#define CAM_ROWS 7
#include "crosscall-addrmod-body.h"
