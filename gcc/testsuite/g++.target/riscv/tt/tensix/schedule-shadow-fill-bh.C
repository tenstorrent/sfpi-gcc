// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-latency-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-times "Shadow-fill moved uid=\\d+ into the bubble after uid=\\d+ target=bh" 2 "rvtt_schedule" } }
// { dg-final { scan-assembler-not "SFPNOP" } }

#include "schedule-shadow-fill-body.h"
