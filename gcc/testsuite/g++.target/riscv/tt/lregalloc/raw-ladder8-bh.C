// Pressure-ladder control rung: exactly 8 simultaneously live vector
// values fill the LREG file exactly and compile TODAY.
// FUTURE-VERDICT (LREG allocator): compile, allocator is a NO-OP --
// .text must be byte-identical to the pre-allocator compiler (gated by
// tools/lreg-arsenal-gate.sh, no-op-below-9 claim).
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }

#define LADDER_NAME ladder_live8
#define LADDER_N 8
#define LADDER_FMT 4 /* INT32: 32-bit Dst rows */
#define LADDER_NOINC 7
#define LADDER_TRIPS 8
#define LADDER_TWIST 0
#include "ladder-body.h"
