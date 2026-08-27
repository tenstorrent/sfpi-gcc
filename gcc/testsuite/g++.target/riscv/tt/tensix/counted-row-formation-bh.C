// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-counted-row-formation -fdump-rtl-rvtt_replay" }
// Fire witness: the parameterized rows canonicalize (register rotation
// rewritten to the recorded registers, the varying immediates excluded
// and delivered between launches) and ONE parameterized record forms.
// Lane ID (final lockstep audit): the previous expected formation here
// (0,4 record + 2 launches from a 15-rename cross-swap plan) was WRONG
// CODE -- decoded value flow shows the launches replaying reads of
// registers the plan's own renames had evacuated (the audit's named
// refusal, counted-row-final-lockstep-divergence).  The surviving
// sound plan forms one 5-word record with one launch; the audit's
// refusal of the old plan is itself a witness.
// { dg-final { scan-rtl-dump "counted-row-final-lockstep-divergence" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Canonicalized counted-row family" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Formed counted-row record" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 5, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 5, 0, 0" 1 } }

#include "counted-row-formation-body.h"
