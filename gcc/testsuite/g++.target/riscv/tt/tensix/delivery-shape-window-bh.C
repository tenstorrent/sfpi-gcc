// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-delivery-shape -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-replay-hoist -fdump-tree-rvtt_delivery_shape" }
// Check that a six-word row under auto-increment selects windowed replay delivery.
// { dg-final { scan-tree-dump "requested unroll 8 of loop" "rvtt_delivery_shape" } }
// { dg-final { scan-tree-dump "mode group-rerecord" "rvtt_delivery_shape" } }
// { dg-final { scan-assembler "TTREPLAY\t0, \[0-9\]+, 1, 1" } }
// { dg-final { scan-assembler "TTREPLAY\t0, \[0-9\]+, 0, 0" } }

#include "delivery-shape-body-w6.h"
