// Check that the list scheduler interleaves two degree-8 Horner chains within the LREG file.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// nodes=34; baseline 64 = 34 words + 30 stalls, interleaved 34.
// { dg-final { scan-rtl-dump-times "List-schedule: bb \\d+ nodes=34 makespan 64 -> 34 target=bh" 1 "rvtt_schedule" } }

namespace ckernel { volatile unsigned long *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

void pq_rational_d8 ()
{
  vFloat x = dst_reg[0];
  vFloat p = x * 3.5f + 0.4375f;
  p = p * x + -2.75f;
  p = p * x + 1.125f;
  p = p * x + -4.5f;
  p = p * x + 0.9375f;
  p = p * x + -1.75f;
  p = p * x + 6.5f;
  p = p * x + -0.375f;
  vFloat q = x * -1.25f + 5.5f;
  q = q * x + -0.625f;
  q = q * x + 2.25f;
  q = q * x + -0.875f;
  q = q * x + 3.25f;
  q = q * x + -6.75f;
  q = q * x + 0.75f;
  q = q * x + 1.5f;
  dst_reg[0] = p;
  dst_reg[1] = q;
}
