// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump-times "Counted-loop replay payload.*length 4" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 2 } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 1 } }

#include "replay-counted-trailing-increment-body.h"
