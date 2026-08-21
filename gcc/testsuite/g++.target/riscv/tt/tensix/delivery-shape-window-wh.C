// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-delivery-shape -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-replay-hoist -fdump-tree-rvtt_delivery_shape" }
// Window fire on Wormhole: same typed admission, same measured table.
// { dg-final { scan-tree-dump "requested unroll 8 of loop" "rvtt_delivery_shape" } }
// { dg-final { scan-assembler "TTREPLAY\t0, \[0-9\]+, 1, 1" } }

#define DS_MODE 0
#include "delivery-shape-body-w6.h"
