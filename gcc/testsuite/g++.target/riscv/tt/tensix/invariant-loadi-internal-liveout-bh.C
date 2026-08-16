// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 3 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "left in loop by LREG pressure" 3 "rvtt_invariant" } }

#include "invariant-loadi-internal-liveout-body.h"
