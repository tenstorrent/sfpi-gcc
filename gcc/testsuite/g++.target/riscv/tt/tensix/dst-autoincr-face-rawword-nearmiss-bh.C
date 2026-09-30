// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -fdump-rtl-rvtt_dst_autoincr-details" }
// Near misses: raw `.ttinsn' words one architectural field away from the
// pure Dst/RWC class, plus a non-constant operand.  Every one must keep
// the refusing default and the dominating placement must refuse BY NAME,
// leaving the per-row TTINCRWC in place:
//   (a) SFPCONFIG-class opcode (0x91) -- not a SETRWC word at all;
//   (b) clear_ab_vld != 0 -- SrcA/SrcB bank-valid handshake effect;
//   (c) bit_mask selects SrcA too (mask=5) -- foreign counter write;
//   (d) bit_mask selects the fidelity reset (mask=0xc);
//   (e) a register operand -- no constant word to decode.
// { dg-final { scan-rtl-dump-times "Dst-autoincr: dominating placement refused: foreign effect on a path .loop \[0-9\]+." 5 "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "preheader" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 10 } }
// { dg-final { scan-assembler-not "TTSETC16" } }

#define FACE_MODE 7

#define FACE_ADVANCE                                                          \
  asm volatile (".ttinsn %0"                                                  \
		:: "n" ((0x91u << 24) | (4u << 18) | (8u << 14) | 4u))
#define FACE_FN nearmiss_sfpconfig_class
#include "dst-autoincr-face-domloop-body.h"
#undef FACE_ADVANCE
#undef FACE_FN

#define FACE_ADVANCE                                                          \
  asm volatile (".ttinsn %0"                                                  \
		:: "n" ((0x37u << 24) | (1u << 22) | (4u << 18)               \
			| (8u << 14) | 4u))
#define FACE_FN nearmiss_clear_ab_vld
#include "dst-autoincr-face-domloop-body.h"
#undef FACE_ADVANCE
#undef FACE_FN

#define FACE_ADVANCE                                                          \
  asm volatile (".ttinsn %0"                                                  \
		:: "n" ((0x37u << 24) | (4u << 18) | (8u << 14) | 5u))
#define FACE_FN nearmiss_srca_mask
#include "dst-autoincr-face-domloop-body.h"
#undef FACE_ADVANCE
#undef FACE_FN

#define FACE_ADVANCE                                                          \
  asm volatile (".ttinsn %0"                                                  \
		:: "n" ((0x37u << 24) | (4u << 18) | (8u << 14) | 0xcu))
#define FACE_FN nearmiss_fidelity_mask
#include "dst-autoincr-face-domloop-body.h"
#undef FACE_ADVANCE
#undef FACE_FN

#define FACE_ADVANCE                                                          \
  asm volatile (".ttinsn %0" :: "r" (nfaces))
#define FACE_FN nearmiss_register_word
#include "dst-autoincr-face-domloop-body.h"
#undef FACE_ADVANCE
#undef FACE_FN
