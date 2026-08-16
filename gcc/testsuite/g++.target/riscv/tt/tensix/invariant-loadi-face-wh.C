// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 4 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "function has opaque LREG state" "rvtt_invariant" } }
// { dg-final { scan-assembler-times "SFPLOADI" 4 } }
// { dg-final { scan-assembler-times {TTSETRWC\t0, 4, 8, 0, 0, 4} 2 } }

#define FACE_FN typed_face_loop_wh
#define FACE_TRIPS 4
#define FACE_ROWS 8
#define FACE_K0 0x3e4b1a3d
#define FACE_K1 0xbf91c2e7
#define FACE_ADVANCE __builtin_rvtt_ttdstface ()
#include "invariant-loadi-face-body.h"
