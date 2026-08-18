// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-counted-row-formation -fdump-rtl-rvtt_replay" }
// Fire witness: the parameterized rows canonicalize (register rotation
// rewritten to the recorded registers, the varying immediates excluded
// and delivered between launches) and ONE parameterized record forms.
// { dg-final { scan-rtl-dump "Canonicalized counted-row family" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Formed counted-row record" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 4, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 4, 0, 0" 2 } }

#include "counted-row-formation-body.h"
