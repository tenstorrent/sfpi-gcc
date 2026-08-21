// Reschedule-proof control: the lane-DS ten-ring lives in a self-loop
// row, which defers whole by name (the cyclic seam is capture
// rotation's territory), and the named pressure refusal is preserved
// byte-identically -- the flag never turns an unsolvable ring into
// anything but the honest error.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-pressure-schedule-prera -fdump-rtl-rvtt_lp_schedule_prera-details" }
// { dg-final { scan-rtl-dump "Prera-pressure-schedule deferred: cyclic row adjacency in bb \\d+ \\(capture rotation owns the backedge seam\\)" "rvtt_lp_schedule_prera" } }
// { dg-final { scan-rtl-dump-not "Prera-pressure-schedule: bb" "rvtt_lp_schedule_prera" } }
// { dg-error "lreg-pressure-exceeded" "" { target *-*-* } 0 }

#define LADDER_NAME prera_ring_live10
#define LADDER_N 10
#define LADDER_FMT 4
#define LADDER_NOINC 7
#define LADDER_TRIPS 8
#define LADDER_TWIST 0
#include "../lregalloc/ladder-body.h"
