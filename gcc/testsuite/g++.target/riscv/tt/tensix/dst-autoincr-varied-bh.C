// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Varied stride and row count: six rows of stride four.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 6 stride 4 config 3 words" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 4" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 6" 6 } }

#define DST_MODE 7
#define DST_STRIDE 4
#define DST_ROWS 6
#define DST_ADDR 0
#include "dst-autoincr-explicit-body.h"
