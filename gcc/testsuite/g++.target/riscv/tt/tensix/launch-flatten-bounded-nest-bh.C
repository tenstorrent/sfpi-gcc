// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -mtt-tensix-optimize-launch-flatten-bounded -fdump-tree-rvtt_launch_flatten -fdump-tree-rvtt_launch_flatten_reset" }
// The inner delivery loop's count depends on the outer induction
// variable, so it is not constant before cunroll; it is requested
// against its proven bound, the outer loop as a nest of it, and cunroll
// flattens both: 4 + 2 swaps, straight line.
// { dg-final { scan-tree-dump "launch-flatten: requested complete unroll of loop \[0-9\]+ \\(~\[0-9\]+ delivery words/trip, trips <= \[0-9\]+\\)" "rvtt_launch_flatten" } }
// { dg-final { scan-tree-dump "launch-flatten: requested complete unroll of loop \[0-9\]+ \\(~\[0-9\]+ delivery words/trip, trips 2, nest\\)" "rvtt_launch_flatten" } }
// { dg-final { scan-tree-dump "launch-flatten: 0 bounded requests left standing after cunroll, cleared" "rvtt_launch_flatten_reset" } }
// { dg-final { scan-assembler-times "SFPSWAP" 6 } }

#define LF_STEPS
#include "launch-flatten-bounded-body.h"
