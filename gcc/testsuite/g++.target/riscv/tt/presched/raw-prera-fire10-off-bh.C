// Check that the ten-live wide XOR body is refused without the pre-RA pressure scheduler.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }

#define FIRE_NAME fire_wide_ten_off
#define FIRE_ROW(i) (2 * (i))
#define FIRE_OUT 192
#define FIRE_FMT 4
#define FIRE_NOINC 7
#include "fire-wide-body.h"
