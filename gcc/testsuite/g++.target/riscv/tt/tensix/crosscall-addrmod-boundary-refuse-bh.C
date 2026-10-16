// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-crosscall-addrmod -fdump-rtl-rvtt_dst_autoincr-details" }
// Check that too few rows to cover the drain window across the call boundary refuses by name.
// { dg-final { scan-rtl-dump "mod-write-dominates-crosscall-body" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "addrmod-hoist: placed" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTSETC16" } }

#define CAM_MODE 7
#define CAM_STRIDE 2
#define CAM_ROWS 6
#include "crosscall-addrmod-body.h"
