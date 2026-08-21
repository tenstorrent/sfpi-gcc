// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-delivery-shape -fdump-tree-rvtt_delivery_shape" }
// Tiny (four-word) row: the replay-safe span (three slots) is below the
// former's MIN_SEQUENCE, so no window can form; the measured table
// still prices unrolled straight-push ahead of the rolled loop
// (loop-control amortization -- the DX-F2 tiny-row class).  The solver
// requests the unroll; the copies deliver explicitly, no capture.
// { dg-final { scan-tree-dump "requested unroll 8 of loop" "rvtt_delivery_shape" } }
// { dg-final { scan-tree-dump "mode unrolled-explicit" "rvtt_delivery_shape" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }
// { dg-final { scan-assembler-times "SFPLOAD\t" 8 } }

#include "delivery-shape-body-tiny.h"
