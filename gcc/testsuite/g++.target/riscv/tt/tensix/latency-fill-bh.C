// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-latency-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-times "Latency-fill moved .* target=bh" 1 "rvtt_schedule" } }

#include "latency-fill-body.h"
#include "latency-fill-ineligible-body.h"
