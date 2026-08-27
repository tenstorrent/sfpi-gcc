// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Varied stride on Wormhole: twelve rows of stride four (the program
// costs 3*2 config-class slots + 2 entry residual = 8, so twelve rows
// remain profitable).
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 12 stride 4 config 3 words" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-not "TTSETC16\t25," } }
// { dg-final { scan-assembler-times "TTSETC16\t29, 4" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 2" 12 } }

#define DST_MODE 3
#define DST_STRIDE 4
#define DST_ROWS 12
#define DST_ADDR 0
#include "dst-autoincr-explicit-body.h"
