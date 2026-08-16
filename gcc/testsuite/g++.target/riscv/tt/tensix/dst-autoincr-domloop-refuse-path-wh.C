// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// { dg-final { scan-rtl-dump "Dst-autoincr: dominating placement refused: foreign effect on a path .loop \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "preheader" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 1 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

#define DST_MODE 3
#define DST_STRIDE 2
#define DST_ADDR 0
#define DST_ARM asm volatile (""); acc += ix
#define DOMLOOP_FN dom_refuse
#include "dst-autoincr-domloop-body.h"
