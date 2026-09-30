// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-replay-loop-unroll -fdump-tree-rvtt_replay_unroll" }
// QSR has no validated replay capability audit for this request.
// { dg-final { scan-tree-dump "refused .replay-loop-unroll-qsr-unproven." "rvtt_replay_unroll" } }

#include "replay-loop-unroll-body.h"
