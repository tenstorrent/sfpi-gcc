// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti" }
// Check that without the flag the rolled row loop is unchanged.
// { dg-final { scan-assembler-not "TTREPLAY" } }
// { dg-final { scan-assembler-times "SFPLOAD\t" 1 } }

#include "delivery-shape-body-tiny.h"
