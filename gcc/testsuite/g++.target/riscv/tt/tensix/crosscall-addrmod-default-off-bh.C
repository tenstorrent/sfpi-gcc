// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Flag off: the contract machinery never runs and the
// per-execution refusal is byte-identical.
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: unprofitable group .config.entry slots 8 >= removed 8" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "addrmod-hoist" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "crosscall-addrmod-unproven" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 8 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

#define CAM_MODE 7
#define CAM_STRIDE 2
#define CAM_ROWS 8
#include "crosscall-addrmod-body.h"
