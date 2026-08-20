// Generality twin of raw-ladder16-bh.C: renamed function, reversed ring
// direction, disjoint Dst rows (64..).  Any allocator behavior keyed to
// the primary rung's names/rows/orientation fails here.
// TODAY: refuses by name.  FUTURE-VERDICT: compile + bit-exact golden
// (twin golden ladder16b, see ARSENAL.md).
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
