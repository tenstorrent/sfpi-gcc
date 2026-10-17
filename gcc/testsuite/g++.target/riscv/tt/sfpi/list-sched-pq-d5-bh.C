// Check that the list scheduler interleaves two degree-5 Horner chains to hide every mad-family stall.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// nodes=22; baseline 40 = 22 words + 18 stalls, interleaved 22.
// { dg-final { scan-rtl-dump-times "List-schedule: bb \\d+ nodes=22 makespan 40 -> 22 target=bh" 1 "rvtt_schedule" } }

namespace ckernel { volatile unsigned long *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

void pq_rational_d5 ()
{
  vFloat x = dst_reg[0];
  vFloat p = x * 2.75f + -1.125f;
  p = p * x + 4.5f;
  p = p * x + 0.375f;
  p = p * x + -0.625f;
  p = p * x + 1.5f;
  vFloat q = x * -0.4375f + 1.25f;
  q = q * x + -6.5f;
  q = q * x + 0.9375f;
  q = q * x + 5.5f;
  q = q * x + -3.25f;
  dst_reg[0] = p;
  dst_reg[1] = q;
}
