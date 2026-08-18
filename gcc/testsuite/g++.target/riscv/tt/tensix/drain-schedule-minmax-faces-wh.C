// Wormhole mirror of drain-schedule-minmax-faces-bh.C: the same proof
// over the WH capability tables' derived calendar -- nothing in the
// boundary placement is per-CPU beyond the tables it consumes.
// SFPNOP 12 -> 3; launch words and separators unchanged from
// loadmacro-periodic-minmax-inplace-faces-wh.C.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 67 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466299904" 16 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467348480" 16 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2473623552" 32 } }
// { dg-final { scan-assembler-times "SFPLOAD\\t" 32 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-times {TTSETRWC\t0, 4, 8, 0, 0, 4} 6 } }
// { dg-final { scan-assembler-not "SFPSWAP" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-rtl-dump-times "run-boundary drain elided" 3 "rvtt_macro_planner" } }

#define MINMAX_FOUR_FACE_RUNS 1
#include "loadmacro-periodic-minmax-inplace-body.h"
