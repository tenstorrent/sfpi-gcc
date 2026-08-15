// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "function has opaque LREG state" 1 "rvtt_invariant" } }

#include "invariant-loadi-opaque-live-body.h"
