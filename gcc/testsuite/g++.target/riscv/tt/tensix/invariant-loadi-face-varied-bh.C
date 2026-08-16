// Varied-constant, varied-trip variant of the typed face-loop shape: the
// decisions key on structure, never on particular values or counts.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 4 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "function has opaque LREG state" "rvtt_invariant" } }

#define FACE_FN varied_face_loop
#define FACE_TRIPS 3
#define FACE_ROWS 5
#define FACE_K0 0x40490fdb
#define FACE_K1 0x00003a29
#define FACE_ADVANCE __builtin_rvtt_ttdstface ()
#include "invariant-loadi-face-body.h"
