// Wormhole twin of the launch-loop unroll fire shape.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump-times "Counted-loop replay payload" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Unrolled launch loop bb \\d+: 20 trips x 2 delivered words" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 21 } }
// { dg-final { scan-assembler-times "TTINCRWC" 20 } }
// { dg-final { scan-assembler-not "\\tbne\\t" } }

#include "replay-launch-unroll-body.h"
