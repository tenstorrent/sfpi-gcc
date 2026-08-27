// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Two 5-row groups share one dominating slot program: neither pays for the
// per-execution program cost alone (5 <= 3*2 config-class slots + 2 entry
// residual = 8), together they do (10 > 8).  The non-Dst TTINCRWC
// separator survives untransformed.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 5 stride 2 config 3 words" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 5 stride 2 shared config" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTSETC16\t18, 0" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t53, 0" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 0, 1, 0" 1 } }
// { dg-final { scan-assembler-not "TTINCRWC\t0, 2, 0, 0" } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 6" 10 } }

#define DST_MODE 7
#define DST_STRIDE 2
#define DST_ADDR 0
#define DST_GROUP1 5
#define DST_GROUP2 5
#define SHARED_ROW row
#define SHARED_FN shared_rows
#include "dst-autoincr-shared-body.h"
