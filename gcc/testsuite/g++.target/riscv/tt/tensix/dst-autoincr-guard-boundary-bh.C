// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Distance-guard boundary: the load and multiply provide exactly the
// two-word settle distance the guard requires, so anchoring the program
// before them transforms the rows.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 9 stride 2 config 3 words" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "below guard" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 6" 9 } }

#define DST_MODE 7
#define DST_STRIDE 2
#define DST_ROWS 9
#define DST_ADDR 0
#include "dst-autoincr-explicit-body.h"
