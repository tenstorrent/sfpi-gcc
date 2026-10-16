// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-loop-unroll -fdump-tree-rvtt_replay_unroll" }
// Check that the replay loop unroll is refused on QSR.
// { dg-final { scan-tree-dump "refused .replay-loop-unroll-qsr-unproven." "rvtt_replay_unroll" } }

#include "replay-loop-unroll-body.h"
