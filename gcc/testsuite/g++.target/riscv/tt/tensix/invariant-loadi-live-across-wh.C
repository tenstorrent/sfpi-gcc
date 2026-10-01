// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 4 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "left in loop by LREG pressure" 2 "rvtt_invariant" } }

#define LIVE_ACROSS_LOOP 1
#include "invariant-loadi-pressure-body.h"
