// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Sixteen explicit rows: well above the per-execution slot break-even
// (removed 16 > 3*2 config-class slots + 2 entry residual = 8), so the
// straight-line group fires.  The eight-row boundary lives in the
// perexec-refuse/perexec-boundary-fire twins.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 16 stride 2 config 3 words" 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t18, 0" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t53, 0" 1 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 6" 16 } }
// { dg-final { scan-assembler-times "SFPLOAD\tL., 0, 0, 7" 16 } }

#define DST_MODE 7
#define DST_STRIDE 2
#define DST_ROWS 16
#define DST_ADDR 0
#include "dst-autoincr-explicit-body.h"
