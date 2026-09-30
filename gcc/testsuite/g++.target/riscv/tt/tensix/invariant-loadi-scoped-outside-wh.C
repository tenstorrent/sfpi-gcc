// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 2 "rvtt_invariant" } }
// The multi-block extension also examines the OUTER loop as a hoist
// candidate; its latch asm is inside that region, so it refuses --
// exactly once, without disturbing the inner hoists.
// { dg-final { scan-tree-dump-times "function has opaque LREG state" 1 "rvtt_invariant" } }

#include "invariant-loadi-scoped-outside-body.h"
