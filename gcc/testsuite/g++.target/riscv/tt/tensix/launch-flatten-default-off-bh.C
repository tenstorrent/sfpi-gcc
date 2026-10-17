// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti" }
// Check that without -mtt-tensix-optimize-launch-flatten the delivery loop stays rolled.
// { dg-final { scan-assembler-times "TTREPLAY\t16, 9, 0, 0" 3 } }

#include "launch-flatten-body.h"
