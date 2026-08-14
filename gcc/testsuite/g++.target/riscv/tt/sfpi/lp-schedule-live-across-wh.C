// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-pressure-schedule -fdump-tree-rvtt_lp_schedule" }
// { dg-final { scan-tree-dump "SFPU pressure region:.*ops=2.*live-in=4.*peak=4" "rvtt_lp_schedule" } }

namespace ckernel {
unsigned *instrn_buffer;
}

#include <sfpi.h>

using namespace sfpi;

void
test()
{
  vFloat x0 = l_reg[LRegs::LReg0];
  vFloat x1 = l_reg[LRegs::LReg1];
  vFloat x2 = l_reg[LRegs::LReg2];
  vFloat sentinel = l_reg[LRegs::LReg3];

  vFloat sum0 = x0 + x1;
  vFloat sum1 = x2 + x0;

  // The write is a hard scheduling boundary.  Sentinel is untouched by the
  // arithmetic region but remains physically live across it.
  l_reg[LRegs::LReg4] = sum0;
  l_reg[LRegs::LReg5] = sum1;
  l_reg[LRegs::LReg6] = sentinel;
}
