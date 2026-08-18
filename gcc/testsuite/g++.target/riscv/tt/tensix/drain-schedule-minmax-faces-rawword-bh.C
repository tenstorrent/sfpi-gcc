// Drain-aware boundary placement under the LLK-PRISTINE upstream shape:
// the face advance is two canonical raw `.ttinsn %0' SETRWC Dst-step
// words (the TTI_ macro shape).  Each word field-decodes to the pure
// Dst/RWC separator class (rvtt-raw-boundary.cc) and is exactly one
// issued word by the extraction contract, so the boundary earns the same
// two slots of credit as the typed advance and every boundary elides:
// SFPNOP 12 -> 3, separators retained as raw words.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 5 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 73 } }
// { dg-final { scan-assembler-times "\\.ttinsn 923926532" 6 } }
// { dg-final { scan-assembler-times "SFPLOAD\\t" 32 } }
// { dg-final { scan-assembler-times "SFPNOP" 3 } }
// { dg-final { scan-assembler-not "SFPSWAP" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-rtl-dump-times "run-boundary drain elided" 3 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "drain-boundary: drain=3 separator-credit=2 words-per-row=3" 3 "rvtt_macro_planner" } }

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
