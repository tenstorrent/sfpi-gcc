// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 4 stride 2 config 3 words .preheader." 1 "rvtt_dst_autoincr" } }
// (On Wormhole the default replay formation folds the four rows into a
// capture-exec plus three launches; the launch word covers one crossing
// slot.)
// { dg-final { scan-rtl-dump "Dst-autoincr: mod-write backedge crossing priced .rows 4, uncovered crossing slots 1, bb \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-not "TTSETC16\t25," } }
// { dg-final { scan-assembler-times "TTSETC16\t29, 2" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 2" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY" 4 } }

#define DST_MODE 3
#define DST_STRIDE 2
#define DST_ADDR 0
#define DST_ARM acc += ix
#define DOMLOOP_FN dom_loop
#include "dst-autoincr-domloop-body.h"
