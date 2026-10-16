// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-counted-row-formation -fdump-rtl-rvtt_replay" }
// Check that rows differing only in immediates and register rotation form one parameterized replay record.
// { dg-final { scan-rtl-dump "counted-row-final-lockstep-divergence" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Canonicalized counted-row family" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Formed counted-row record" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 5, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 5, 0, 0" 1 } }

#include "counted-row-formation-body.h"
