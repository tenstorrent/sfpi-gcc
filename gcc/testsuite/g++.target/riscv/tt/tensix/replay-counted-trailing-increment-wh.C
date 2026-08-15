// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -fdump-rtl-rvtt_replay-details" }
// { dg-final { scan-rtl-dump-times "Counted-loop replay payload" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY" 2 } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 1 } }

#include "replay-counted-trailing-increment-body.h"
