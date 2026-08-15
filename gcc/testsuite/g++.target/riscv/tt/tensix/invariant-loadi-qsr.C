// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "hoist refused on QSR" 1 "rvtt_invariant" } }
// { dg-final { scan-assembler-times "SFPLOADI" 4 } }

#include "invariant-loadi-body.h"
