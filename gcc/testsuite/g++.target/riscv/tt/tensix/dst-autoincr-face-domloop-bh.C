// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// A face loop whose body ends in the TYPED face advance: the advance is a
// pure RWC counter step by typed identity (configuration-window legal), so
// the dominating placement proof covers every path through the body and
// the three-word program lands once in the dedicated face-loop preheader,
// under the SETC16-to-consume distance guard.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 2 stride 2 config 3 words .preheader." 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t18, 0" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t53, 0" 1 } }
// { dg-final { scan-assembler-times {TTSETRWC\t0, 4, 8, 0, 0, 4} 2 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 6" 2 } }

#define FACE_MODE 7
#define FACE_ADVANCE __builtin_rvtt_ttdstface ()
#define FACE_FN face_dom_loop
#include "dst-autoincr-face-domloop-body.h"
