// An explicit "#pragma GCC unroll 1" must keep governing what it has
// always governed here: the payload-duplicating complete-unroll REQUEST.
// Exactly one request (the unannotated control), and the invariant hoist
// still fires for both loops (6 immediates each).  Control unrolls to 8
// replays at gimple and the in-block replay former captures its repeated
// rows.  Under the interlock-aware reissue pricing the annotated loop's
// serially-chained residual is execution-bound: the counted-loop hoist
// REFUSES it (dump arithmetic below) and the annotated loop keeps its
// scalar backedge -- the pragma and the corrected cost model agree.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-tree-rvtt_invariant-details -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-tree-dump-times "Requested complete unroll for constant replay loop" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 12 "rvtt_invariant" } }
// { dg-final { scan-rtl-dump "Not hoisting: modeled benefit -251 < 0" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Unrolled launch loop" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 8 } }
// { dg-final { scan-assembler "\\tbne\\t" } }

#include "invariant-replay-unroll-pragma-body.h"
