// The production kernel shape under the LLK-PRISTINE upstream headers:
// rows=32 runs=4 in-place face runs separated by the RAW face advance
// (two canonical `.ttinsn %0' SETRWC Dst-step words per boundary, the
// TTI_ macro shape).  Each word field-decodes to the pure Dst/RWC
// separator class, so formation is byte-for-byte the typed
// loadmacro-periodic-minmax-inplace-faces-bh.C except the separators
// print as raw words (6 more .ttinsn: the typed test's TTSETRWC x6).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner" }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 73 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2466308096" 16 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2467356672" 16 } }
// { dg-final { scan-assembler-times "\\.ttinsn\\t2473639936" 32 } }
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
