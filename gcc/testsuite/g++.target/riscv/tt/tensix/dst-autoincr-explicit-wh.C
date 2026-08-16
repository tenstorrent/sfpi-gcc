// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Wormhole programs both physical slots behind the two-bit modifier field
// (bank base ownership is not provable), so the configuration is six words.
// Replay is disabled so this exercises the explicit-row shape (the RAW
// hazard fill otherwise makes these short rows replay-formable).
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 8 stride 2 config 6 words" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t11, 0" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t25, 2" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t50, 0" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t19, 0" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t29, 2" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t54, 0" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 2" 8 } }
// { dg-final { scan-assembler-times "SFPLOAD\tL., 0, 0, 3" 8 } }

#define DST_MODE 3
#define DST_STRIDE 2
#define DST_ROWS 8
#define DST_ADDR 0
#include "dst-autoincr-explicit-body.h"
