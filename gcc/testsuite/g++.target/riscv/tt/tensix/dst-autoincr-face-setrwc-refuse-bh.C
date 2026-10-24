// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Check that a TTSETRWC that also resets SrcA/SrcB (mask 15) refuses the dominating placement.
// { dg-final { scan-rtl-dump "Dst-autoincr: dominating placement refused: foreign effect on a path .loop \[0-9\]+." "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "preheader" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 2 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

#define FACE_MODE 7
#define FACE_ADVANCE __builtin_rvtt_ttsetrwc (0, 0, 0, 0, 0, 15)
#define FACE_FN face_setrwc_refuse
#include "dst-autoincr-face-domloop-body.h"
