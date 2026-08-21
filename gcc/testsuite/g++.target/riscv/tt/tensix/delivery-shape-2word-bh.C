// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-delivery-shape -fdump-tree-rvtt_delivery_shape" }
// Two-word row (the fixed-factor pass's row-too-small class): the
// window legs refuse by name -- no payload fits between the former's
// MIN_SEQUENCE and the capture budget -- and the solver still wins the
// delivery arbitration with the unrolled explicit stream.
// { dg-final { scan-tree-dump "window legs refused .delivery-shape-window-budget." "rvtt_delivery_shape" } }
// { dg-final { scan-tree-dump "requested unroll 8 of loop" "rvtt_delivery_shape" } }
// { dg-final { scan-tree-dump "mode unrolled-explicit" "rvtt_delivery_shape" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }

#include "delivery-shape-body-2word.h"
