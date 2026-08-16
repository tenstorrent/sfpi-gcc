// An explicit "#pragma GCC unroll 1" must keep its scalar loop: exactly one
// unroll request (the unannotated control), one surviving backedge (the
// annotated loop), and the invariant hoist still fires for both loops
// (6 immediates each).  Control unrolls to 3 replays; the annotated loop
// keeps its in-loop record+launch pair.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-replay-hoist -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Requested complete unroll for constant replay loop" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 12 "rvtt_invariant" } }
// { dg-final { scan-assembler-times "TTREPLAY" 5 } }
// { dg-final { scan-assembler-times "\\tbne\\t" 1 } }

#include "invariant-replay-unroll-pragma-body.h"
