// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-delivery-shape -fdump-tree-rvtt_delivery_shape" }
// The same six-word row WITHOUT the auto-increment context: the
// surviving separator words and boundary costs price every window leg
// at or above the rolled stream at the conservative boundary end.
// The solver selects rolled, annotates factor 1 (owning the decision
// slot), and the object code stays byte-identical -- no repeats, no
// capture.
// { dg-final { scan-tree-dump "selected rolled .rolled-explicit. for loop" "rvtt_delivery_shape" } }
// { dg-final { scan-tree-dump-not "requested unroll" "rvtt_delivery_shape" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }

#include "delivery-shape-body-w6.h"
