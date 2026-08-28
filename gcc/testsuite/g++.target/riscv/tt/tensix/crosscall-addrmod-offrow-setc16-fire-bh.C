// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-crosscall-addrmod -fdump-rtl-rvtt_dst_autoincr-details" }
// Precision control for the clobber twin: the SAME caller-loop raw
// SETC16 word aimed at a NON-owned row (35 = a foreign slot's register)
// decodes off-contract and the contract still fires -- the census
// refuses writers of the OWNED rows, not the opcode class.
// { dg-final { scan-rtl-dump "Dst-autoincr crosscall-addrmod contract: rows 8 stride 2" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "crosscall-addrmod-owned-row-write" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#define CAM_MODE 7
#define CAM_STRIDE 2
#define CAM_ROWS 8
#define CAM_CALLER_EXTRA \
  asm volatile (".ttinsn %0" :: "n" ((0xb2u << 24) | (35u << 16) | 4u))
#include "crosscall-addrmod-body.h"
