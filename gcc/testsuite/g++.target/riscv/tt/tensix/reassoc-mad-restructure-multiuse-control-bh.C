// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc-mad-restructure -fdump-tree-rvtt_combine" }
// Multi-use near-miss control: the product has a SECOND consumer (a
// store), so the pair is NOT the single-use contract-fuse shape -- the
// restructure does not claim it (that class belongs to the multi-use
// licensed mad-fuse behind -mtt-tensix-optimize-reassoc), the
// immediate fold proceeds, and no restructure or refusal line prints.
// { dg-final { scan-tree-dump-not "mad restructure" "rvtt_combine" } }
// { dg-final { scan-tree-dump-not "refusing" "rvtt_combine" } }
// { dg-final { scan-assembler "SFPMULI" } }
// { dg-final { scan-assembler-not "SFPMAD\t" } }
#define MRB_KERNEL mrb_multiuse_control
#define MRB_MULTIUSE 1
#include "reassoc-mad-restructure-body.h"
