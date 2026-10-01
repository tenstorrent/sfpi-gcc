// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-delivery-shape -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-replay-hoist -fdump-tree-rvtt_delivery_shape" }
// Six-word row under the auto-increment context: the separators are
// absorbed after formation, the five-slot safe window prices ahead of
// the rolled stream at both boundary ends, and the always-on former
// captures the unrolled row as one execute-while-record pass plus
// one-word playback launches per group.
// { dg-final { scan-tree-dump "requested unroll 8 of loop" "rvtt_delivery_shape" } }
// { dg-final { scan-tree-dump "mode group-rerecord" "rvtt_delivery_shape" } }
// { dg-final { scan-assembler "TTREPLAY\t0, \[0-9\]+, 1, 1" } }
// { dg-final { scan-assembler "TTREPLAY\t0, \[0-9\]+, 0, 0" } }

#include "delivery-shape-body-w6.h"
