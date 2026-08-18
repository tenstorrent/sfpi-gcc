// The LLK-pristine face loop: the latch carries the RAW face advance (two
// canonical `.ttinsn %0' SETRWC Dst-step words).  Each decodes to the
// pure Dst/RWC class (rvtt-raw-boundary.cc), so the face-loop region is
// non-opaque and both loads hoist twice, exactly like the typed
// invariant-loadi-face-bh.C; the raw words stay in the body.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 4 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "function has opaque LREG state" "rvtt_invariant" } }
// { dg-final { scan-assembler-times "SFPLOADI" 4 } }
// { dg-final { scan-assembler-times {\.ttinsn 923926532} 2 } }

#define FACE_FN rawword_face_loop
#define FACE_TRIPS 4
#define FACE_ROWS 8
#define FACE_K0 0x3e4b1a3d
#define FACE_K1 0xbf91c2e7
#define FACE_ADVANCE                                                          \
  do                                                                          \
    {                                                                         \
      asm volatile (".ttinsn %0"                                              \
		    :: "n" ((0x37u << 24) | (4u << 18) | (8u << 14) | 4u));   \
      asm volatile (".ttinsn %0"                                              \
		    :: "n" ((0x37u << 24) | (4u << 18) | (8u << 14) | 4u));   \
    }                                                                         \
  while (0)
#include "invariant-loadi-face-body.h"
