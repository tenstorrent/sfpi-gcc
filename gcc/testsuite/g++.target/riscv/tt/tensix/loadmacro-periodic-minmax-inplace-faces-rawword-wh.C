// Wormhole variant of the rawword production shape (SETRWC shares the
// WH/BH encoding): formation identical to the typed
// loadmacro-periodic-minmax-inplace-faces-wh.C except the separators
// print as raw words (6 more .ttinsn).
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 73 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466299904" 16 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467348480" 16 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2473623552" 32 } }
// { dg-final { scan-assembler-times "\\.ttinsn 923926532" 6 } }
// { dg-final { scan-assembler-times "SFPLOAD\\t" 32 } }
// { dg-final { scan-assembler-times "SFPNOP" 12 } }
// { dg-final { scan-assembler-not "TTSETRWC\t0, 4, 8, 0, 0, 4" } }
// { dg-final { scan-assembler-not "SFPSWAP" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }

#define MINMAX_FOUR_FACE_RUNS 1
#define MINMAX_FACE_ADVANCE()                                                 \
  do                                                                          \
    {                                                                         \
      asm volatile (".ttinsn %0"                                              \
		    :: "n" ((0x37u << 24) | (4u << 18) | (8u << 14) | 4u));   \
      asm volatile (".ttinsn %0"                                              \
		    :: "n" ((0x37u << 24) | (4u << 18) | (8u << 14) | 4u));   \
    }                                                                         \
  while (0)
#include "loadmacro-periodic-minmax-inplace-body.h"
