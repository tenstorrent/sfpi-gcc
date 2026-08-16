// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Wormhole variant: the dual-slot rule programs both physical slots (six
// words), still once, in the face-loop preheader above the typed advance.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 2 stride 2 config 6 words .preheader." 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t25, 2" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t29, 2" 1 } }
// { dg-final { scan-assembler-times {TTSETRWC\t0, 4, 8, 0, 0, 4} 2 } }
// On Wormhole the hazard SFPNOP makes the row four words, so the default
// replay formation captures it (one literal store, one launch); the
// terminator inside the recording carries the implicit advance.
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 2" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY" 2 } }

#define FACE_MODE 3
#define FACE_ADVANCE __builtin_rvtt_ttdstface ()
#define FACE_FN face_dom_loop_wh
#include "dst-autoincr-face-domloop-body.h"
