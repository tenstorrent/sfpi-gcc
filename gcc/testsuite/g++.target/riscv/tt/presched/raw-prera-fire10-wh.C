// WH leg of the fire twin (no-increment address mode 3): the audited
// logical-family facts cover both targets, so the same wide order
// fires on Wormhole.
// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-pressure-schedule-prera -fdump-rtl-rvtt_lp_schedule_prera-details" }
// { dg-final { scan-rtl-dump "Prera-pressure-schedule: bb \\d+ nodes=15 pressure 10 -> 7 makespan 15 -> 15 model-peak=7 candidate=model target=wh" "rvtt_lp_schedule_prera" } }

#define FIRE_NAME fire_wide_ten_wh
#define FIRE_ROW(i) (2 * (i))
#define FIRE_OUT 192
#define FIRE_FMT 4
#define FIRE_NOINC 3
#include "fire-wide-body.h"
