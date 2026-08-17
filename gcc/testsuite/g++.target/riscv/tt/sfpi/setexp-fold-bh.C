// setexp-fold at the sfpi level: the exp-kernel recombine idiom
// setexp (frac, exexp (z, Biased)) becomes the single copy-form SFPSETEXP.
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-setexp-fold" }

namespace ckernel{
  extern volatile unsigned instrn_buffer[];
}
#include <sfpi.h>

using namespace sfpi;

void exp_recombine () {
  vFloat z = l_reg[LRegs::LReg0];
  vFloat frac = l_reg[LRegs::LReg1];
  vInt e = exexp (z, ExponentMode::Biased);
  vFloat y = setexp (frac, e);
  l_reg[LRegs::LReg0] = y;
}

// Near miss: the unbiased exponent is not the raw field.
void refuse_unbiased () {
  vFloat z = l_reg[LRegs::LReg0];
  vFloat frac = l_reg[LRegs::LReg1];
  vInt e = exexp (z, ExponentMode::Unbiased);
  vFloat y = setexp (frac, e);
  l_reg[LRegs::LReg2] = y;
}

// { dg-final { scan-assembler-times {SFPSETEXP\tL[0-9]+, L[0-9]+, 0, 2} 1 } }
// { dg-final { scan-assembler-times {SFPSETEXP\tL[0-9]+, L[0-9]+, 0, 0} 1 } }
// { dg-final { scan-assembler-times {SFPEXEXP} 1 } }
