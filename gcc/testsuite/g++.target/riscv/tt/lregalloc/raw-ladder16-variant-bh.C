// Check the 16-live refusal with a renamed function, reversed ring and different Dst rows.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }

#define LADDER_NAME ladder_live16_rev
#define LADDER_N 16
#define LADDER_FMT 4
#define LADDER_NOINC 7
#define LADDER_TRIPS 8
#define LADDER_TWIST 1
#include "ladder-body.h"
