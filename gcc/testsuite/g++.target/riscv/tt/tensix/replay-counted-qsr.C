// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump-times "Counted-loop replay payload.*length 4" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 2 } }

#include "replay-counted-body.h"
