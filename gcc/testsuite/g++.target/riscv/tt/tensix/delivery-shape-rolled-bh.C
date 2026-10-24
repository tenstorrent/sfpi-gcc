// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-delivery-shape -fdump-tree-rvtt_delivery_shape" }
// Check that without auto-increment the six-word row selects rolled and annotates factor 1.
// { dg-final { scan-tree-dump "selected rolled .rolled-explicit. for loop" "rvtt_delivery_shape" } }
// { dg-final { scan-tree-dump-not "requested unroll" "rvtt_delivery_shape" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }

#include "delivery-shape-body-w6.h"
