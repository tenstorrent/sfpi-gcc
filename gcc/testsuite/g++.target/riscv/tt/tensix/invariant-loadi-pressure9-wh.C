// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "LREG pressure 9 exceeds 8" 1 "rvtt_invariant" } }

#define NINE_LREG_PRESSURE 1
#include "invariant-loadi-pressure-body.h"
