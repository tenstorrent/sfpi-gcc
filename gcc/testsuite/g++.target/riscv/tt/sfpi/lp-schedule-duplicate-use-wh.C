// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-pressure-schedule -fdump-tree-rvtt_lp_schedule" }
// { dg-final { scan-tree-dump "SFPU pressure schedule:.*old-peak=9.*new-peak=8.*validated=yes.*reason=ok.*applied=yes" "rvtt_lp_schedule" } }

namespace ckernel {
unsigned *instrn_buffer;
}

#include <sfpi.h>

using namespace sfpi;

sfpi_inline void
update(vFloat x, vFloat &mean, vFloat &m2, vFloat recip)
{
    vFloat delta = x - mean;
    mean += delta * recip;
    vFloat delta2 = x - mean;
    m2 += delta * delta2;
}

void
test()
{
    vFloat x0 = l_reg[LRegs::LReg0];
    vFloat x1 = l_reg[LRegs::LReg1];
    vFloat x2 = l_reg[LRegs::LReg2];
    vFloat x3 = l_reg[LRegs::LReg3];
    vFloat mean = l_reg[LRegs::LReg4];
    vFloat m2 = l_reg[LRegs::LReg5];
    vFloat bias = l_reg[LRegs::LReg6];
    vFloat recip = l_reg[LRegs::LReg7];

    update(x0, mean, m2, recip);
    update(x1, mean, m2, recip);
    update(x2, mean, m2, recip);

    vFloat folded = x3 + bias;
    // Exercise exact duplicate-use accounting in both the scheduler and the
    // independent validator.
    vFloat squared = folded * folded;
    update(squared, mean, m2, recip);

    l_reg[LRegs::LReg4] = mean;
    l_reg[LRegs::LReg5] = m2;
}
