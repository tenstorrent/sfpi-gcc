// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti" }
// Default-off: without the flag the rolled row loop delivers its whole
// row every trip -- no request, no repeats, no capture.
// { dg-final { scan-assembler-not "TTREPLAY" } }
// { dg-final { scan-assembler-times "SFPLOAD\t" 1 } }

#include "delivery-shape-body-tiny.h"
