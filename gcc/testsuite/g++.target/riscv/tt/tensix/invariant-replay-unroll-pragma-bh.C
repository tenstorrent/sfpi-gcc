// An explicit "#pragma GCC unroll 1" must keep governing what it has
// always governed here: the payload-duplicating complete-unroll REQUEST.
// Exactly one request (the unannotated control), and the invariant hoist
// still fires for both loops (6 immediates each).  Control unrolls to 8
// replays at gimple.  The annotated loop's payload is never replicated:
// the pragma-blind post-reload replay machinery (hoist, then launch-loop
// unroll) records its capture once and unrolls only the residual delivery
// loop into back-to-back launches -- a delivery transformation of the
// same class as the hoist, outside the pragma's payload-duplication
// scope (the per-loop request does not survive the post-reload loop
// rebuild; if it ever does, the launch-loop unroll defers to it).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-tree-rvtt_invariant-details -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-tree-dump-times "Requested complete unroll for constant replay loop" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 12 "rvtt_invariant" } }
// { dg-final { scan-assembler-times "TTREPLAY" 17 } }
// { dg-final { scan-rtl-dump-times "Unrolled launch loop bb \\d+: 8 trips x 1 delivered words" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-not "\\tbne\\t" } }

#include "invariant-replay-unroll-pragma-body.h"
