// Check that 9 live values with only bf16 Dst rows are refused rather than spilled inexactly.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }
// { dg-error "lreg-pressure-exceeded|lreg-spill-inexact-dst-mode" "" { target *-*-* } 0 }

#define LADDER_NAME ladder_live9_bf16
#define LADDER_N 9
#define LADDER_FMT 2 /* FP16B: a 32-bit spill to these rows would truncate.  */
#define LADDER_NOINC 7
#define LADDER_TRIPS 8
#define LADDER_TWIST 0
#include "ladder-body.h"
