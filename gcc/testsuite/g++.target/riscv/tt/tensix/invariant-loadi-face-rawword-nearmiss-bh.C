// Near miss at the gimple hoist layer: the latch word is SFPCONFIG-class
// (opcode 0x91), not a pure Dst/RWC SETRWC, so the raw decode refuses,
// the face region stays opaque, and the face-level hoist refuses by name;
// the inner loop still hoists (2 loads, once).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 2 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "function has opaque LREG state" 1 "rvtt_invariant" } }

#define FACE_FN rawword_face_loop_nearmiss
#define FACE_TRIPS 4
#define FACE_ROWS 8
#define FACE_K0 0x3e4b1a3d
#define FACE_K1 0xbf91c2e7
#define FACE_ADVANCE                                                          \
  asm volatile (".ttinsn %0"                                                  \
		:: "n" ((0x91u << 24) | (4u << 18) | (8u << 14) | 4u))
#include "invariant-loadi-face-body.h"
