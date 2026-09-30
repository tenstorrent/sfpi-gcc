// CONTROL twin of raw-prera-firefloat-bh.C: refuses by name without
// the pre-RA pressure scheduler.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }

#define FIREF_NAME fire_float_ten_off
#define FIREF_ROW(i) (2 * (i))
#define FIREF_OUT 192
#define FIREF_NOINC 7
#include "fire-float-body.h"
