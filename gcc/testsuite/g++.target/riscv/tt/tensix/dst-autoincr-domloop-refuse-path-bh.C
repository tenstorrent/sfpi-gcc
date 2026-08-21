// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// A foreign effect (opaque asm) on ONE path through the loop body refuses
// the dominating preheader placement.  The fall-back per-group program
// inside the body re-executes every iteration: three configuration words
// plus the live crossing's drain residual against only four removed
// increments, so the function is emitted unchanged -- refusal, never
// unsoundness.
// { dg-final { scan-rtl-dump "Dst-autoincr: dominating placement refused: foreign effect on a path .loop \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "preheader" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group:" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 4 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

#define DST_MODE 7
#define DST_STRIDE 2
#define DST_ADDR 0
#define DST_ARM asm volatile (""); acc += ix
#define DOMLOOP_FN dom_refuse
#include "dst-autoincr-domloop-body.h"
