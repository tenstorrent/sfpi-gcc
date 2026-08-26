// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti" }
// Default-off: without the flag the typed spelling's inflated size
// estimate keeps the delivery loop rolled -- the loop-control words
// stay in the timed path and the static launch sites stay per-arm.
// (Generic peeling copies one iteration; the rolled arms keep two more
// static launch sites -- nothing like the 31 flattened playbacks.)
// { dg-final { scan-assembler-times "TTREPLAY\t16, 9, 0, 0" 3 } }

#include "launch-flatten-body.h"
