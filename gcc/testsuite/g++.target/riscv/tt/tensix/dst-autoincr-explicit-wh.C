// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -mno-tt-tensix-optimize-replay -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Check explicit-row Dst auto-increment on Wormhole: only physical slot 6 (SETC16
// 19/29/54) is written, never the LLK's base-0 slot 2.  Replay is disabled.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 16 stride 2 config 3 words" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t19, 0" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t29, 2" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t54, 0" 1 } }
// { dg-final { scan-assembler-not "TTSETC16\t11," } }
// { dg-final { scan-assembler-not "TTSETC16\t25," } }
// { dg-final { scan-assembler-not "TTSETC16\t50," } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 2" 16 } }
// { dg-final { scan-assembler-times "SFPLOAD\tL., 0, 0, 3" 16 } }

#define DST_MODE 3
#define DST_STRIDE 2
#define DST_ROWS 16
#define DST_ADDR 0
#include "dst-autoincr-explicit-body.h"
