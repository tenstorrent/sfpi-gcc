// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-crosscall-addrmod -fdump-rtl-rvtt_dst_autoincr-details" }
// Check that replay-delivered mod-write rows on WH refuse the contract by name.
// { dg-final { scan-rtl-dump "crosscall-addrmod-unproven .replay-delivered-row." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: unprofitable payload family" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "addrmod-hoist: placed" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTSETC16" } }

#define CAM_MODE 3
#define CAM_STRIDE 2
#define CAM_ROWS 8
#include "crosscall-addrmod-body.h"
