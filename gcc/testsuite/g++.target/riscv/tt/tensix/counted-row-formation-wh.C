// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-counted-row-formation -fdump-rtl-rvtt_replay" }
// WH twin of the counted-row fire witness.
// Final-lockstep audit: under WH's register rotation every
// canonicalization plan for this body relied on post-verification
// occupancy swaps that broke the member words' operand correspondence
// (the launches would replay reads of registers the plan's own renames
// evacuated -- wrong code, the same defect decoded on the BH twin).
// The audit refuses every plan by name and NO record forms; this twin
// is now the refusal witness.
// { dg-final { scan-rtl-dump "counted-row-final-lockstep-divergence" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Formed counted-row record" "rvtt_replay" } }

#include "counted-row-formation-body.h"
