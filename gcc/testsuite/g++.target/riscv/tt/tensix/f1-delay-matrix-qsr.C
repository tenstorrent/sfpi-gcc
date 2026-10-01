// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mno-tt-tensix-optimize-dce -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-assembler-times "SFPNOP" 11 } }
// { dg-final { scan-rtl-dump-times "Inserting dynamic nop after" 7 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "Not inserting dynamic nop after" 6 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "Inserting static nop after" 2 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "Not inserting static nop after" 1 "rvtt_schedule" } }

#include "f1-delay-matrix-body.h"

void dynamic_qsr_only()
{
  auto a = __builtin_rvtt_sfpreadlreg(0);
  auto b = __builtin_rvtt_sfpreadlreg(1);
  auto r = __builtin_rvtt_sfpmad(__builtin_rvtt_sfpreadlreg(11), a, b, 0);
  __builtin_rvtt_sfpwritelreg(__builtin_rvtt_sfpnonlinear(r, 4), 3);
}
