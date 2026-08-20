// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti" }
// Default-off: without the flag the rolled row loop delivers its whole
// row every trip -- no unroll request, no repeats, no replay capture.
// { dg-final { scan-assembler-not "TTREPLAY" } }

#include "replay-loop-unroll-body.h"
