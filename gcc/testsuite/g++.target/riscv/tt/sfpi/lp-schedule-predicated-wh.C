// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-pressure-schedule -fdump-tree-rvtt_lp_schedule" }
// { dg-final { scan-tree-dump "SFPU pressure region:.*rejected=cc-epoch" "rvtt_lp_schedule" } }
// { dg-final { scan-tree-dump-not "applied=yes" "rvtt_lp_schedule" } }

namespace ckernel {
unsigned *instrn_buffer;
}

#include <sfpi.h>

using namespace sfpi;

void
test()
{
  vFloat x = l_reg[LRegs::LReg0];
  vFloat y = l_reg[LRegs::LReg1];
  vFloat result = x;

  v_if (x < y)
  {
    vFloat product = x * y;
    result = product + x;
  }
  v_endif;

  l_reg[LRegs::LReg2] = result;
}
