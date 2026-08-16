// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Wormhole dual-slot configuration costs six words: two 4-row groups pay
// for it only through the shared dominating program.  The four-word rows
// exceed the replay minimum on this target, so the shared groups are
// launch-shaped: one executing capture plus seven playbacks.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 4 stride 2 config 6 words" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 4 stride 2 shared config" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTSETC16\t25, 2" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t29, 2" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 0, 1, 0" 1 } }
// { dg-final { scan-assembler-not "TTINCRWC\t0, 2, 0, 0" } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 2" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY" 8 } }

#define DST_MODE 3
#define DST_STRIDE 2
#define DST_ADDR 0
#define DST_GROUP1 4
#define DST_GROUP2 4
#define SHARED_ROW row
#define SHARED_FN shared_rows
#include "dst-autoincr-shared-body.h"
