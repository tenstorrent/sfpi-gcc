// Check the bf16 9-value refusal with a reversed ring over different rows.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }
// { dg-error "lreg-pressure-exceeded|lreg-spill-inexact-dst-mode" "" { target *-*-* } 0 }

#define LADDER_NAME ladder_live9_bf16_rev
#define LADDER_N 9
#define LADDER_FMT 2
#define LADDER_NOINC 7
#define LADDER_TRIPS 8
#define LADDER_TWIST 1
#include "ladder-body.h"
