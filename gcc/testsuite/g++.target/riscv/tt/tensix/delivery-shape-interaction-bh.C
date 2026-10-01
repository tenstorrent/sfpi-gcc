// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-delivery-shape -mtt-tensix-optimize-replay-loop-unroll -fdump-tree-rvtt_delivery_shape -fdump-tree-rvtt_replay_unroll" }
// Both request passes enabled: the solver decides first and its rolled
// selection (loop->unroll = 1) preempts the fixed-factor request pass,
// which never overrides an existing annotation -- one model, one
// decision.
// { dg-final { scan-tree-dump "selected rolled .rolled-explicit. for loop" "rvtt_delivery_shape" } }
// { dg-final { scan-tree-dump-not "requested unroll" "rvtt_replay_unroll" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }

#include "delivery-shape-body-w6.h"
