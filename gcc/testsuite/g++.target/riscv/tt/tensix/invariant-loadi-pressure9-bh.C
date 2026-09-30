// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 5 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "left in loop by LREG pressure" 2 "rvtt_invariant" } }

#define NINE_LREG_PRESSURE 1
#include "invariant-loadi-pressure-body.h"
