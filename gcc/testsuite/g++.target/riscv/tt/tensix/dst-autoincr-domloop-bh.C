// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Multi-block loop body (conditional scalar arm): the dominating placement
// proof covers every path through the body, and the program lands once in
// the dedicated preheader.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 1 stride 2 config 3 words .preheader." 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 6" 1 } }

#define DST_MODE 7
#define DST_STRIDE 2
#define DST_ADDR 0
#define DST_ARM acc += ix
#define DOMLOOP_FN dom_loop
#include "dst-autoincr-domloop-body.h"
