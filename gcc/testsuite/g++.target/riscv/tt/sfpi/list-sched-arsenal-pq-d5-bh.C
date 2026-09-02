// List-scheduler acceptance arsenal: dual-Horner P/Q rational,
// degree 5 (mid width of the three-width interleave family; see the
// d3 twin for the full dependence argument).
//
// P(x) = ((((2.75 x - 1.125) x + 4.5) x + 0.375) x - 0.625) x + 1.5
// Q(x) = ((((-0.4375 x + 1.25) x - 6.5) x + 0.9375) x + 5.5) x - 3.25
// Non-special dyadic coefficients (bf16-exact, no preloaded-constant
// value): no constant-register fold participates.
//
// Hand dependence analysis: two independent serial mad-family RAW
// chains of five Horner steps each (audited latency 1 per rvtt-cost.md
// D3).  Serial emission carries one modeled stall per dependent
// adjacency inside each chain; the interleaved order hides every
// shadow in the other chain's next word.  The longer width checks that
// the win SCALES: more dependent adjacencies mean strictly more hidden
// stalls than d3, and the makespan oracle pins scheduled == lower
// bound here too.
//
// simulator-golden on the pinned reference simulator.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// Measured (pinned): nodes=22, baseline 40 = 22 words + 18 stalls,
// interleaved 22 = word count.  Oracle: 43 -> 25 == lower bound.
// { dg-final { scan-rtl-dump-times "List-schedule: bb \\d+ nodes=22 makespan 40 -> 22 target=bh" 1 "rvtt_schedule" } }

namespace ckernel { unsigned *instrn_buffer; }
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
