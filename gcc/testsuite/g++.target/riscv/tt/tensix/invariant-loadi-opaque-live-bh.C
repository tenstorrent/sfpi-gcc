// Historically a whole-function refusal: any opaque assembly anywhere in
// the function blocked every loop.  Under the region-scoped ownership
// proof the assembly before the loop and after it lies outside the hoist
// region -- every path from the hoist point to a use of the hoisted value
// stays inside {preheader tail, loop body} -- so this shape now hoists.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "function has opaque LREG state" "rvtt_invariant" } }

#include "invariant-loadi-opaque-live-body.h"
