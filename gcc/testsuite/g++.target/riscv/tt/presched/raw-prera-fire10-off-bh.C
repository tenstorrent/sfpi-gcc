// CONTROL twin of raw-prera-fire10-bh.C: without the pre-RA pressure
// scheduler the same body refuses by name -- ten simultaneously live
// values cannot allocate in the eight-register file.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }

#define FIRE_NAME fire_wide_ten_off
#define FIRE_ROW(i) (2 * (i))
#define FIRE_OUT 192
#define FIRE_FMT 4
#define FIRE_NOINC 7
#include "fire-wide-body.h"
