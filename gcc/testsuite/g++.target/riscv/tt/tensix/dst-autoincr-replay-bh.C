// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 8 stride 2 config 3 words" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// The payload store appears once (in the recording) and is retargeted to the
// auto-increment modifier; the loads keep the no-increment modifier.
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 6" 1 } }
// { dg-final { scan-assembler-times "SFPLOAD\tL., 0, 0, 7" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY" 8 } }

#define DST_MODE 7
#include "dst-autoincr-replay-body.h"
