// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-crosscall-addrmod -fdump-rtl-rvtt_dst_autoincr-details" }
// Check that a caller-loop raw SETC16 to a non-owned row does not block the contract.
// { dg-final { scan-rtl-dump "Dst-autoincr crosscall-addrmod contract: rows 8 stride 2" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "crosscall-addrmod-owned-row-write" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#define CAM_MODE 7
#define CAM_STRIDE 2
#define CAM_ROWS 8
#define CAM_CALLER_EXTRA \
  asm volatile (".ttinsn %0" :: "n" ((0xb2u << 24) | (35u << 16) | 4u))
#include "crosscall-addrmod-body.h"
