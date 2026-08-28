// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-crosscall-addrmod -fdump-rtl-rvtt_dst_autoincr-details" }
// Slot-clobber NEAR-MISS (the ordered twin): the caller loop delivers a
// raw SETC16 word to owned row 34 (ADDR_MOD_DST_SEC6) between calls --
// with the program hoisted, the next call's rows would consume the
// clobbered stride.  The census decodes the word (0xb2 opcode, row 34)
// and MUST refuse; the callee keeps its per-execution refusal
// byte-identically (TTINCRWC rows, no TTSETC16 anywhere).
// { dg-final { scan-rtl-dump "crosscall-addrmod-unproven .crosscall-addrmod-owned-row-write." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "Dst-autoincr refusal: unprofitable group" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "addrmod-hoist: placed" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 8 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

#define CAM_MODE 7
#define CAM_STRIDE 2
#define CAM_ROWS 8
#define CAM_CALLER_EXTRA \
  asm volatile (".ttinsn %0" :: "n" ((0xb2u << 24) | (34u << 16) | 4u))
#include "crosscall-addrmod-body.h"
