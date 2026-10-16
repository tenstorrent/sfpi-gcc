// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-delivery-shape -mtt-tensix-delivery-shape-min-benefit=100000 -fdump-tree-rvtt_delivery_shape" }
// Check that a benefit below the override threshold selects rolled.
// { dg-final { scan-tree-dump "selected rolled" "rvtt_delivery_shape" } }
// { dg-final { scan-tree-dump-not "requested unroll" "rvtt_delivery_shape" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }

#include "delivery-shape-body-tiny.h"
