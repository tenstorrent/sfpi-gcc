// Check that the list scheduler interleaves two degree-3 Horner chains to hide every mad-family stall.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// nodes=14 (2 chain-seed copies + 12 mad-family words); baseline 24 =
// 14 words + 10 chain stalls, interleaved 14.
// { dg-final { scan-rtl-dump-times "List-schedule: bb \\d+ nodes=14 makespan 24 -> 14 target=bh" 1 "rvtt_schedule" } }

namespace ckernel { volatile unsigned long *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

void pq_rational_d3 ()
{
  vFloat x = dst_reg[0];
  vFloat p = x * 1.5f + -2.25f;
  p = p * x + 0.75f;
  p = p * x + 3.5f;
  vFloat q = x * -3.25f + 1.75f;
  q = q * x + -0.875f;
  q = q * x + 2.5f;
  dst_reg[0] = p;
  dst_reg[1] = q;
}
