// The RTL proof must check current physical slot initialization, rather
// than treating a cached source-TU audit as permission for any later MOP.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-prgm-const -fdump-rtl-rvtt_lreg_livein" }
// { dg-final { scan-rtl-dump-times {Lane merge: insn [0-9]+ keeps the old value of LREG mask 0x02} 3 "rvtt_lreg_livein" } }

#ifndef RVTT_MOP_POSITIVE_ONLY
#define RVTT_MOP_NEGATIVE_ONLY 1
#endif

#define WRITE(N) do { RAW (0x71120000 | (N)); \
  __builtin_rvtt_sfprawlreg_effect (0, 2); } while (0)
#define CHECK_LANES() do { WRITE (1); WRITE (2); \
  __builtin_rvtt_sfpencc_all_lanes (); \
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpreadlreg (1), 4); \
} while (0)

#include "mop-lane-must-init-body.h"
