// Check that the 9-live ring is refused on Wormhole.
// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops" }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }

#define LADDER_NAME ladder_live9_wh
#define LADDER_N 9
#define LADDER_FMT 4
#define LADDER_NOINC 3
#define LADDER_TRIPS 8
#define LADDER_TWIST 0
#include "ladder-body.h"
