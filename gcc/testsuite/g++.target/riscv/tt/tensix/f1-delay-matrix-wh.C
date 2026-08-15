// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mno-tt-tensix-optimize-dce -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-assembler-times "SFPNOP" 15 } }
// { dg-final { scan-rtl-dump-times "Inserting dynamic nop after" 11 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "Not inserting dynamic nop after" 1 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "Inserting static nop after" 2 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "Not inserting static nop after" 1 "rvtt_schedule" } }

#include "f1-delay-matrix-body.h"
