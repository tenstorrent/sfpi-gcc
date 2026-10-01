// Pressure-ladder rung 9 under DEFAULT flags: the named refusal must
// fire without any -mtt flag, and the relief-flag note must appear.
// TODAY: refuses by name.  FUTURE-VERDICT: compile + bit-exact golden.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }
// { dg-message "proven-constant values" "" { target *-*-* } 0 }

#define LADDER_NAME ladder_live9_default
#define LADDER_N 9
#define LADDER_FMT 4
#define LADDER_NOINC 7
#define LADDER_TRIPS 8
#define LADDER_TWIST 0
#include "ladder-body.h"
