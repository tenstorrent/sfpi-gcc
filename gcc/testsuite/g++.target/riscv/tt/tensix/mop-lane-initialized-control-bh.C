// Positive control in a TU with no uninitialized public MOP launch.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mtt-tensix-optimize-prgm-const -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }
// { dg-final { scan-tree-dump-times "rotate chain collapse 8 -> 0 links" 1 "rvtt_crosslane" } }
// { dg-final { scan-tree-dump-not "crosslane-lane-state-unproven" "rvtt_crosslane" } }
// { dg-final { scan-assembler-not "SFPSHFT2" } }

#define RVTT_MOP_POSITIVE_ONLY 1
#include "mop-lane-must-init-bh.C"
