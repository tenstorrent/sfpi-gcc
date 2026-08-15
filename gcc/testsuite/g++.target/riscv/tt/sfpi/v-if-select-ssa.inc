namespace ckernel {
unsigned *instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

void select_uint ()
{
  vFloat condition = l_reg[LRegs::LReg0];
  vUInt on_true = l_reg[LRegs::LReg1];
  vUInt result = l_reg[LRegs::LReg2];
  v_if (condition != 0.0f)
    {
      result = on_true;
    }
  v_endif;
  l_reg[LRegs::LReg3] = result;
}

void select_float ()
{
  vFloat condition = l_reg[LRegs::LReg0];
  vFloat on_true = l_reg[LRegs::LReg1];
  vFloat result = l_reg[LRegs::LReg2];
  v_if (condition != 0.0f)
    {
      result = on_true;
    }
  v_endif;
  l_reg[LRegs::LReg3] = result;
}
