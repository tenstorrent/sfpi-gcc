// A single-trip face loop still proves its first iteration structurally;
// hoisting above it executes the loads exactly as often as before.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 4 "rvtt_invariant" } }

#define FACE_FN single_trip_face_loop
#define FACE_TRIPS 1
#define FACE_ROWS 8
#define FACE_K0 0x3e4b1a3d
#define FACE_K1 0xbf91c2e7
#define FACE_ADVANCE __builtin_rvtt_ttdstface ()
#include "invariant-loadi-face-body.h"
