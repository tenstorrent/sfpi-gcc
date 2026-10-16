// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti" }
// Check that without the flag the row loop is not unrolled and no replay is formed.
// { dg-final { scan-assembler-not "TTREPLAY" } }

#include "replay-loop-unroll-body.h"
