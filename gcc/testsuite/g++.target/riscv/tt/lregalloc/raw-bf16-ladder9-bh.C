// Refusal edge: 9 simultaneously live values in a kernel whose ONLY Dst
// layout evidence is bf16 (every load/store uses mod0 FMT_FP16B = 2,
// i.e. 16-bit Dst rows).  A Dst-row spill of a 32-bit LREG value would
// TRUNCATE: an exact-only spiller must refuse, by name.
// TODAY: refuses lreg-pressure-exceeded (no spiller exists).
// FUTURE-VERDICT (LREG allocator): NAMED REFUSAL
// lreg-spill-inexact-dst-mode -- compiling this silently is an arsenal
// FAIL, and so is a fall-through to a spill that truncates.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }
// { dg-error "lreg-pressure-exceeded|lreg-spill-inexact-dst-mode" "" { target *-*-* } 0 }

#define LADDER_NAME ladder_live9_bf16
#define LADDER_N 9
#define LADDER_FMT 2 /* FP16B: 16-bit Dst rows -- spill would truncate */
#define LADDER_NOINC 7
#define LADDER_TRIPS 8
#define LADDER_TWIST 0
#include "ladder-body.h"
