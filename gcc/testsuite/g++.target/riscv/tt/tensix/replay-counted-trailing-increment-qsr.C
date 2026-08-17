// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump-times "Counted-loop replay payload.*length 4" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Unrolled launch loop bb \\d+: 8 trips x 2 delivered words" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 9 } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 8 } }

#include "replay-counted-trailing-increment-body.h"
