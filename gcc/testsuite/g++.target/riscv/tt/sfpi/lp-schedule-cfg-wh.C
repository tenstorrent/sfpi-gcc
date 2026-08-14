// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lp-schedule -fdump-tree-rvtt_lp_schedule" }
// { dg-final { scan-tree-dump "SFPU pressure region:.*rejected=cfg" "rvtt_lp_schedule" } }
// { dg-final { scan-tree-dump-not "applied=yes" "rvtt_lp_schedule" } }

namespace ckernel {
unsigned *instrn_buffer;
}

#include <sfpi.h>

using namespace sfpi;

void
test(unsigned choose_left)
{
  vFloat x = l_reg[LRegs::LReg0];
  vFloat y = l_reg[LRegs::LReg1];
  vFloat result;

  if (choose_left)
    result = (x + y) * x;
  else
    result = (x + y) * y;

  l_reg[LRegs::LReg2] = result;
}
