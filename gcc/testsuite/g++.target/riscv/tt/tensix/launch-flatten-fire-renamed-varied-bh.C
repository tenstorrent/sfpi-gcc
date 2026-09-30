// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-launch-flatten -fdump-tree-rvtt_launch_flatten" }
// Renamed function, varied trip count, window start/length, and raw
// word: the decision is structural, so the request still fires and the
// flattened stream carries the varied shape.
// { dg-final { scan-tree-dump "launch-flatten: requested complete unroll of loop \[0-9\]+ \\(~\[0-9\]+ delivery words/trip, trips 12\\)" "rvtt_launch_flatten" } }
// { dg-final { scan-assembler-times "TTREPLAY\t8, 6, 0, 0" 23 } }
// { dg-final { scan-assembler-times "TTREPLAY\t8, 6, 1, 1" 1 } }
// { dg-final { scan-assembler-times "\\.ttinsn" 6 } }

#define LF_KERNEL some_other_phase_driver
#define LF_TRIPS 12
#define LF_START 8
#define LF_LEN 6
#define LF_CFGWORD 0x91800004u
#include "launch-flatten-body.h"
