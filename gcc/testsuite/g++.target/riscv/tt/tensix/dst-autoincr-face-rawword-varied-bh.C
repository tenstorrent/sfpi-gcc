// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Varied-constant twins of the raw face advance, one per admitted field
// form: a CR-mode step with a DIFFERENT delta (D=4), and the
// current-relative mode (CR bit 3, C_TO_CR).  The decode admits the
// whole architectural class (any pure Dst-leg SETRWC), never the exact
// face-advance word, so both fire exactly like the D=8 pair.
// { dg-final { scan-rtl-dump-times "Dst-autoincr group: bb \[0-9\]+ rows 2 stride 2 config 3 words .preheader." 2 "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTSETC16\t18, 0" 2 } }
// { dg-final { scan-assembler-times "TTSETC16\t34, 2" 2 } }
// { dg-final { scan-assembler-times "TTSETC16\t53, 0" 2 } }
// { dg-final { scan-assembler-times {\.ttinsn 923860996} 2 } }
// { dg-final { scan-assembler-times {\.ttinsn 924975108} 2 } }
// { dg-final { scan-assembler-times "SFPSTORE\tL., 0, 0, 6" 4 } }

#define FACE_MODE 7
#define FACE_ADVANCE                                                          \
  do                                                                          \
    {                                                                         \
      asm volatile (".ttinsn %0"                                              \
		    :: "n" ((0x37u << 24) | (4u << 18) | (4u << 14) | 4u));   \
      asm volatile (".ttinsn %0"                                              \
		    :: "n" ((0x37u << 24) | (4u << 18) | (4u << 14) | 4u));   \
    }                                                                         \
  while (0)
#define FACE_FN face_rawword_varied_delta
#include "dst-autoincr-face-domloop-body.h"

#undef FACE_ADVANCE
#undef FACE_FN
#define FACE_ADVANCE                                                          \
  do                                                                          \
    {                                                                         \
      asm volatile (".ttinsn %0"                                              \
		    :: "n" ((0x37u << 24) | (8u << 18) | (8u << 14) | 4u));   \
      asm volatile (".ttinsn %0"                                              \
		    :: "n" ((0x37u << 24) | (8u << 18) | (8u << 14) | 4u));   \
    }                                                                         \
  while (0)
#define FACE_FN face_rawword_varied_ctocr
#include "dst-autoincr-face-domloop-body.h"
