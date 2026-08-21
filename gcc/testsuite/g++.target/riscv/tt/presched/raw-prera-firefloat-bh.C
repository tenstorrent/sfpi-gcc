// Float-chain fire twin (mad family, audited latency 1 -- the
// xielu/lcm class): ten live as written, eight after the ECC
// candidate, makespan preserved, compiles where the control twin
// refuses.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-pressure-schedule-prera -fdump-rtl-rvtt_lp_schedule_prera-details" }
// { dg-final { scan-rtl-dump "Prera-pressure-schedule: bb \\d+ nodes=12 pressure 10 -> 8 makespan 14 -> 14 model-peak=8 candidate=ecc target=bh" "rvtt_lp_schedule_prera" } }

#define FIREF_NAME fire_float_ten
#define FIREF_ROW(i) (2 * (i))
#define FIREF_OUT 192
#define FIREF_NOINC 7
#include "fire-float-body.h"
