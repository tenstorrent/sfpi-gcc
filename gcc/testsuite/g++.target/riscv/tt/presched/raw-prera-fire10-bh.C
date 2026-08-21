// FIRE twin: the ten-live two-chain wide order becomes schedulable
// within eight registers by reordering alone.  The rvtt_prgm_const
// SSA model (the lane-DS pressure oracle's source of record) certifies
// the as-written pressure of 10 in the SAME compile; the pre-RA
// scheduler's model candidate reaches 7 with the makespan preserved,
// and the kernel that refuses lreg-pressure-exceeded without the flag
// (see raw-prera-fire10-off-bh.C) now compiles.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-pressure-schedule-prera -mtt-tensix-optimize-const-remat -fdump-rtl-rvtt_lp_schedule_prera-details -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "const-remat: pressure 10 exceeds the 8-LREG file" "rvtt_prgm_const" } }
// { dg-final { scan-rtl-dump "Prera-pressure-schedule region: bb=\\d+ nodes=15 live-in=5 base-peak=10 base-makespan=15" "rvtt_lp_schedule_prera" } }
// { dg-final { scan-rtl-dump "Prera-pressure-schedule: bb \\d+ nodes=15 pressure 10 -> 7 makespan 15 -> 15 model-peak=7 candidate=model target=bh" "rvtt_lp_schedule_prera" } }

#define FIRE_NAME fire_wide_ten
#define FIRE_ROW(i) (2 * (i))
#define FIRE_OUT 192
#define FIRE_FMT 4
#define FIRE_NOINC 7
#include "fire-wide-body.h"
