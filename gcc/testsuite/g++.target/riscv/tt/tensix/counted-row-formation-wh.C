// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-counted-row-formation -fdump-rtl-rvtt_replay" }
// Check that on WH the final-lockstep audit refuses every canonicalization
// plan whose occupancy swaps break operand correspondence, so no record forms.
// { dg-final { scan-rtl-dump "counted-row-final-lockstep-divergence" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Formed counted-row record" "rvtt_replay" } }

#include "counted-row-formation-body.h"
