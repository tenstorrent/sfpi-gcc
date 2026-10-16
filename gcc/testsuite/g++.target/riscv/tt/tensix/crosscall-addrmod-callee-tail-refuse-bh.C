// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-crosscall-addrmod -fdump-rtl-rvtt_dst_autoincr-details" }
// Check that a raw SETC16 after the callee's rows rewriting an owned row refuses the contract.
// { dg-final { scan-rtl-dump "crosscall-addrmod-unproven .callee-slot-clobber." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "addrmod-hoist: placed" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 8 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

#define CAM_MODE 7
#define CAM_STRIDE 2
#define CAM_ROWS 8
#define CAM_CALLEE_TAIL \
  asm volatile (".ttinsn %0" :: "n" ((0xb2u << 24) | (34u << 16) | 4u))
#include "crosscall-addrmod-body.h"
