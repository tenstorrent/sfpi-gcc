// Pressure-ladder control: exactly 8 simultaneously live vector values
// fill the LREG file exactly, so the kernel compiles without spilling.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }

#define LADDER_NAME ladder_live8
#define LADDER_N 8
#define LADDER_FMT 4 /* INT32: 32-bit Dst rows */
#define LADDER_NOINC 7
#define LADDER_TRIPS 8
#define LADDER_TWIST 0
#include "ladder-body.h"
