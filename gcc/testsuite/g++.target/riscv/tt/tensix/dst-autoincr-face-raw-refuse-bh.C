// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Check that a non-canonical asm carrying two `.ttinsn' words refuses the preheader
// placement.
// { dg-final { scan-rtl-dump "Dst-autoincr: dominating placement refused: foreign effect on a path .loop \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "preheader" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 2 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

#define FACE_MODE 7
#define FACE_ADVANCE asm volatile (".ttinsn 0x37120004\n\t.ttinsn 0x37120004")
#define FACE_FN face_raw_refuse
#include "dst-autoincr-face-domloop-body.h"
