// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-latency-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "Shadow-fill moved .* target=wh" "rvtt_schedule" } }
// { dg-final { scan-assembler-not "SFPNOP" } }

#include "schedule-shadow-fill-body.h"
