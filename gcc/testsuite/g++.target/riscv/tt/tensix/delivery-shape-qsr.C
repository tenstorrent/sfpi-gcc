// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-delivery-shape -fdump-tree-rvtt_delivery_shape" }
// QSR has no validated replay capability audit for this arbitration.
// { dg-final { scan-tree-dump "refused .delivery-shape-qsr-unproven." "rvtt_delivery_shape" } }

#define DS_MODE 0
#include "delivery-shape-body-tiny.h"
