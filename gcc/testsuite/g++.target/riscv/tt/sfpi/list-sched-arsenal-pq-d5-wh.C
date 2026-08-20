// Lane DT list-scheduler acceptance arsenal: the d5 dual-Horner P/Q
// pair on Wormhole.  Same kernel and dependence argument as the BH d5
// twin; the architectural difference is the witness: WH has no
// scoreboard, so every unhidden mad-family shadow is an EXPLICIT
// required SFPNOP word in the assembly.  The interleave therefore
// shows up as physical word-count reduction (delivered words shrink),
// not just a modeled number -- the strongest form of the win.
// Expect: fire on the region, and the scheduled function body carries
// no SFPNOP between the interleaved chains (any residual NOPs belong
// to boundary words, pinned by count once measured).
// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// Measured (pinned): fire 40 -> 22 and the required-SFPNOP count
// drops 18 -> 0 (physical delivered words).  Oracle: 43 -> 25 ==
// lower bound (the WH baseline's 18 NOP words are real words).
// { dg-final { scan-assembler-not "SFPNOP" } }
// { dg-final { scan-rtl-dump-times "List-schedule: bb \\d+ nodes=22 makespan 40 -> 22 pressure-peak=3 target=wh" 1 "rvtt_schedule" } }

namespace ckernel { unsigned *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

void pq_rational_d5_wh ()
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
