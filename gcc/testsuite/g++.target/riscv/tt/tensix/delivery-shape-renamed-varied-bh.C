// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-delivery-shape -fdump-tree-rvtt_delivery_shape" }
// Renamed function, varied trip count and Dst mode: the decision is
// structural and priced, never keyed to any identity.
// { dg-final { scan-tree-dump "requested unroll 8 of loop" "rvtt_delivery_shape" } }
// { dg-final { scan-tree-dump "mode unrolled-explicit" "rvtt_delivery_shape" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }

#define DS_KERNEL some_other_delivery_engine
#define DS_TRIPS 24
#define DS_MODE 3
#include "delivery-shape-body-tiny.h"
