// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lp-schedule -fdump-tree-rvtt_lp_schedule" }
// { dg-final { scan-tree-dump "SFPU pressure region:.*ops=16.*live-in=7.*peak=8" "rvtt_lp_schedule" } }

namespace ckernel {
unsigned *instrn_buffer;
}

#include <sfpi.h>

using namespace sfpi;

sfpi_inline void
welford_update (vFloat x, vFloat recip, vFloat &mean, vFloat &m2)
{
  vFloat delta = x - mean;
  mean += delta * recip;
  vFloat delta2 = x - mean;
  m2 += delta * delta2;
}

void
welford4 ()
{
  vFloat x0 = l_reg[LRegs::LReg0];
  vFloat x1 = l_reg[LRegs::LReg1];
  vFloat x2 = l_reg[LRegs::LReg2];
  vFloat x3 = l_reg[LRegs::LReg3];
  vFloat mean = l_reg[LRegs::LReg4];
  vFloat m2 = l_reg[LRegs::LReg5];
  vFloat recip = l_reg[LRegs::LReg7];

  welford_update (x0, recip, mean, m2);
  welford_update (x1, recip, mean, m2);
  welford_update (x2, recip, mean, m2);
  welford_update (x3, recip, mean, m2);

  l_reg[LRegs::LReg4] = mean;
  l_reg[LRegs::LReg5] = m2;
}
