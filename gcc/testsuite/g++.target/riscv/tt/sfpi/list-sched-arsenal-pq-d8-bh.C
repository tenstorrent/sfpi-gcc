// List-scheduler acceptance arsenal: dual-Horner P/Q rational,
// degree 8 (widest of the three-width interleave family; see the d3
// twin for the full dependence argument).
//
// P(x) = (((((((3.5 x + 0.4375) x - 2.75) x + 1.125) x - 4.5) x
//           + 0.9375) x - 1.75) x + 6.5) x - 0.375
// Q(x) = (((((((-1.25 x + 5.5) x - 0.625) x + 2.25) x - 0.875) x
//           + 3.25) x - 6.75) x + 0.75) x + 1.5
// Non-special dyadic coefficients (bf16-exact, no preloaded-constant
// value): no constant-register fold participates.
//
// Hand dependence analysis: two independent serial mad-family RAW
// chains of eight Horner steps (audited latency 1, rvtt-cost.md D3).
// This width additionally exercises coefficient-materialization
// traffic under the eight-LREG file: at any point only x, p, q and the
// in-flight coefficient words are live, so the interleaved schedule
// fits the architectural bound (post-allocation the eight hard LREGs are
// asserted by the scheduler's own dispatch walk), while the number of
// hideable one-slot shadows is the largest of the family.  Oracle pins
// scheduled == lower bound.
//
// simulator-golden on the pinned reference simulator.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// Measured (pinned): nodes=34, baseline 64 = 34 words + 30 stalls,
// interleaved 34 = word count.  Oracle: 67 -> 37 == lower bound.
// { dg-final { scan-rtl-dump-times "List-schedule: bb \\d+ nodes=34 makespan 64 -> 34 target=bh" 1 "rvtt_schedule" } }

namespace ckernel { unsigned *instrn_buffer; }
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
