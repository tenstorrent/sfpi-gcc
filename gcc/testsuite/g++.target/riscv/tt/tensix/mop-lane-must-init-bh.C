// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mtt-tensix-optimize-prgm-const -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }
// The independent initialized control lives in a different TU: the public
// negative entries here must poison a complete no-recorder certificate.
// { dg-final { scan-tree-dump-not "rotate chain collapse" "rvtt_crosslane" } }
// { dg-final { scan-tree-dump-times "crosslane-lane-state-unproven" 3 "rvtt_crosslane" } }
// { dg-final { scan-assembler-times {SFPSHFT2\tL[0-9]+, L[0-9]+, 0, 3} 24 } }

#ifndef RVTT_MOP_POSITIVE_ONLY
#define RVTT_MOP_NEGATIVE_ONLY 1
#endif

#define CHECK_LANES() do { auto v = __builtin_rvtt_sfpreadlreg (0); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  v = __builtin_rvtt_sfpshft2_subvec_shfl1 (v, 3); \
  __builtin_rvtt_sfpwritelreg (v, 1); } while (0)

#include "mop-lane-must-init-body.h"
