// Public entry remains rooted even when the TU also defines _start.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -fdump-tree-rvtt_crossloop" }
// { dg-final { scan-tree-dump "hoisted across" "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-not "crossloop-caller-unrooted" "rvtt_crossloop" } }
// { dg-final { scan-assembler "_Z11xlho_kerneli" } }

#define XLHO_KERNEL_LINKAGE
#include "crossloop-hoist-orphan-refuse-bh.C"
