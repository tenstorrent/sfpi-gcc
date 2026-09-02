// List-scheduler acceptance arsenal: dual-Horner P/Q rational,
// degree 3 (the documented interleave case, production sfpi shape).
//
// P(x) = ((1.5 x - 2.25) x + 0.75) x + 3.5
// Q(x) = ((-3.25 x + 1.75) x - 0.875) x + 2.5
// Coefficients are non-special dyadic rationals (bf16-exact, none of
// them a preloaded constant-LREG value), so no constant-register fold
// or LUT shape participates: the decision keys only on the dependence
// structure.
//
// Hand dependence analysis (the expected property):
//   Each Horner step lowers to mad-family words (audited result
//   latency 1, rvtt-cost.md D3 table): the P-chain is a serial RAW
//   chain P1 -> P2 -> ... (issue distance >= words + 1 per dependent
//   adjacency), and the Q-chain likewise.  P and Q share only the
//   read of x (RAR, no edge), so the two chains are INDEPENDENT in the
//   region DAG.  Emitted serially (P fully, then Q), every dependent
//   mad-family adjacency carries one modeled interlock stall; the
//   interleaved order P1 Q1 P2 Q2 ... hides every one-slot result
//   shadow behind the other chain's next word, so the modeled makespan
//   strictly decreases and equals the critical-path lower bound (the
//   makespan oracle, tools/tensix-makespan-oracle.py, pins bound ==
//   scheduled makespan for this kernel).  The dst_reg load/store words
//   are dst-access barriers bounding the region; the coefficient
//   materializations (audited latency-0 loadi words) are extra
//   schedulable fillers.
//
// simulator-golden: this exact kernel body runs bit-exact on the pinned
// the reference simulator with the scheduler ON and OFF.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// Measured (pinned): region nodes=14 (2 chain-seed copies + 12
// mad-family words; the dst load/store words are the region's
// dst-access barriers), baseline 24 = 14 words + 10 chain stalls,
// interleaved 14 = the word count -- even the exit shadow into the
// first store is hidden by the other chain's last word.  Oracle:
// whole-function 27 -> 17 == lower bound (gap 0).
// { dg-final { scan-rtl-dump-times "List-schedule: bb \\d+ nodes=14 makespan 24 -> 14 target=bh" 1 "rvtt_schedule" } }

namespace ckernel { unsigned *instrn_buffer; }
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
