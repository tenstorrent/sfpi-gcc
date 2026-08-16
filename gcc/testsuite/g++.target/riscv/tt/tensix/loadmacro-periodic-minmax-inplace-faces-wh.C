// Wormhole production shape: rows=32 runs=4 in-place face runs, one
// shared descriptor, store-demotion fallback.  Frozen-pass byte-parity
// re-verified offline on this same source.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 70 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466299904" 16 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467348480" 16 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2473623552" 32 } }
// { dg-final { scan-assembler-times "SFPLOAD\\t" 32 } }
// { dg-final { scan-assembler-times "SFPNOP" 12 } }
// { dg-final { scan-assembler-times {TTSETRWC\t0, 4, 8, 0, 0, 4} 6 } }
// { dg-final { scan-assembler-not "SFPSWAP" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#define MINMAX_FOUR_FACE_RUNS 1
#include "loadmacro-periodic-minmax-inplace-body.h"
