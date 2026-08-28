// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-crosscall-addrmod -fdump-rtl-rvtt_dst_autoincr-details" }
// Wormhole bank-select watch row: a caller-loop SETC16 to thread
// configuration address 2 (ADDR_MOD_SET_Base) re-aliases the scratch
// modifier to the base-0 bank without touching the owned slot registers
// -- the watch row refuses it exactly like an owned-row write.
// { dg-final { scan-rtl-dump "crosscall-addrmod-unproven .crosscall-addrmod-owned-row-write." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "addrmod-hoist: placed" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 8 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

#define CAM_MODE 3
#define CAM_STRIDE 2
#define CAM_ROWS 8
#define CAM_CALLER_EXTRA \
  asm volatile (".ttinsn %0" :: "n" ((0xb2u << 24) | (2u << 16) | 0u))
#include "crosscall-addrmod-body.h"
