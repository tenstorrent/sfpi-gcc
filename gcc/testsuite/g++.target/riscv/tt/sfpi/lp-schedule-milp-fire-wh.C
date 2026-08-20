// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-pressure-schedule -mtt-tensix-pressure-schedule-use-milp -fdump-tree-rvtt_lp_schedule" }
// The exact solver rescues a region the greedy list scheduler cannot:
// this 13-operation mad/add DAG has source-order peak 10, the
// deterministic list scheduler's lookahead-1 choice locks it at peak 9,
// but a capacity-8 issue order exists and the branch-and-bound finds
// it.  The companion lp-schedule-milp-off-*.C proves the same region
// refuses (reason=profitability) without the solver.
// { dg-final { scan-tree-dump "SFPU pressure schedule: old-peak=10 new-peak=8 validated=yes reason=ok rejection-selftest=passed applied=yes" "rvtt_lp_schedule" } }
// { dg-final { scan-tree-dump "SFPU MILP: requested=yes backend=bnb.*status=optimal.*selected=yes" "rvtt_lp_schedule" } }

namespace ckernel {
unsigned *instrn_buffer;
}

#include <sfpi.h>

using namespace sfpi;

void
test ()
{
  vFloat a0 = l_reg[LRegs::LReg0];
  vFloat a1 = l_reg[LRegs::LReg1];
  vFloat a2 = l_reg[LRegs::LReg2];
  vFloat a3 = l_reg[LRegs::LReg3];
  vFloat a4 = l_reg[LRegs::LReg4];
  vFloat a5 = l_reg[LRegs::LReg5];
  vFloat a6 = l_reg[LRegs::LReg6];
  vFloat a7 = l_reg[LRegs::LReg7];

  vFloat t0 = a0 * a2 + a5;
  vFloat t1 = a2 + a3;
  vFloat t2 = a6 + a7;
  vFloat t3 = t2 * a7 + t0;
  vFloat t4 = t0 + t1;
  vFloat t5 = a2 + a7;
  vFloat t6 = a3 * t4 + t0;
  vFloat t7 = t3 + a4;
  vFloat t8 = t4 + t7;
  vFloat t9 = t4 * a2 + t5;
  vFloat t10 = a1 + a7;
  vFloat t11 = t7 * a6 + t6;
  vFloat t12 = t11 + t10;

  l_reg[LRegs::LReg0] = t8;
  l_reg[LRegs::LReg1] = t9;
  l_reg[LRegs::LReg2] = t12;
}
