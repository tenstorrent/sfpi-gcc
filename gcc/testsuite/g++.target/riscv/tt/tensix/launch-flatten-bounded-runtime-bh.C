// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -mtt-tensix-optimize-launch-flatten-bounded -fdump-tree-rvtt_launch_flatten" }
// Check that a runtime delivery count with a proven bound of three is requested for complete unroll.
// { dg-final { scan-tree-dump "launch-flatten: requested complete unroll of loop \[0-9\]+ \\(~\[0-9\]+ delivery words/trip, trips <= 3\\)" "rvtt_launch_flatten" } }
// { dg-final { scan-assembler-times "SFPSWAP" 3 } }

#define LF_RUNTIME
#include "launch-flatten-bounded-body.h"
