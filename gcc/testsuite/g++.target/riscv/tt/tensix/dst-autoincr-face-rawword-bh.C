// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// The LLK-pristine upstream face advance: TWO canonical raw `.ttinsn %0'
// SETRWC words (clear=0, CR=4, D=8, B=0, A=0, mask=4 -- the TTI_ macro
// shape, fields composed here, never a magic literal).  Each word is
// field-decoded architecturally as a pure Dst/RWC counter step, the same
// class as the typed face advance, so the dominating placement proof holds
// and the three-word program lands once in the face-loop preheader --
// identical decisions to the typed dst-autoincr-face-domloop-bh.C.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 2 stride 2 config 3 words .preheader." 1 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t18, 0" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 1 } }
// { dg-final { scan-assembler-times "TTSETC16\t53, 0" 1 } }
// { dg-final { scan-assembler-times {\.ttinsn 923926532} 2 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 6" 2 } }

#define FACE_MODE 7
#define FACE_ADVANCE                                                          \
  do                                                                          \
    {                                                                         \
      asm volatile (".ttinsn %0"                                              \
		    :: "n" ((0x37u << 24) | (4u << 18) | (8u << 14) | 4u));   \
      asm volatile (".ttinsn %0"                                              \
		    :: "n" ((0x37u << 24) | (4u << 18) | (8u << 14) | 4u));   \
    }                                                                         \
  while (0)
#define FACE_FN face_rawword
#include "dst-autoincr-face-domloop-body.h"
