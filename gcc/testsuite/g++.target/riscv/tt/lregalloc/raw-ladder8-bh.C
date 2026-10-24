// Check that an 8-live ring, which exactly fills the LREG file, compiles.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }

#define LADDER_NAME ladder_live8
#define LADDER_N 8
#define LADDER_FMT 4 /* INT32: 32-bit Dst rows */
#define LADDER_NOINC 7
#define LADDER_TRIPS 8
#define LADDER_TWIST 0
#include "ladder-body.h"
