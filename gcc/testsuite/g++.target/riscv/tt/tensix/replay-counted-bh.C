// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump-times "Counted-loop replay payload.*length 8" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump "benefit 2241" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Unrolled launch loop bb \\d+: 32 trips x 1 delivered words" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 33 } }
// { dg-final { scan-assembler-not "\\tbne\\t" } }

#include "replay-counted-body.h"
