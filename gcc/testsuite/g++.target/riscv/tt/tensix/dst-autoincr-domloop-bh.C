// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Check that the slot program lands once in the preheader of a multi-block loop body.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 4 stride 2 config 3 words .preheader." 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump "Dst-autoincr: mod-write backedge crossing covered .rows 4, iteration slot words \[0-9\]+ >= drain window 7, bb \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 6" 4 } }

#define DST_MODE 7
#define DST_STRIDE 2
#define DST_ADDR 0
#define DST_ARM acc += ix
#define DOMLOOP_FN dom_loop
#include "dst-autoincr-domloop-body.h"
