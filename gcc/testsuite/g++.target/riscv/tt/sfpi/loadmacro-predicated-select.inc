namespace ckernel { unsigned *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

void predicated_select_in_place ()
{
  vFloat condition = dst_reg[0].mode<DataLayout::F16b> ();
  vUInt on_true = dst_reg[32].mode<DataLayout::U16> ();
  vUInt on_false = dst_reg[64].mode<DataLayout::U16> ();
  vUInt result = on_false;
  v_if (condition != 0.0f)
    {
      result = on_true;
    }
  v_endif;
  dst_reg[0].mode<DataLayout::U16> () = result;
}

void predicated_select_distinct_output ()
{
  vFloat condition = dst_reg[0].mode<DataLayout::F16b> ();
  vUInt on_true = dst_reg[32].mode<DataLayout::U16> ();
  vUInt on_false = dst_reg[64].mode<DataLayout::U16> ();
  vUInt result = on_false;
  v_if (condition != 0.0f)
    {
      result = on_true;
    }
  v_endif;
  dst_reg[96].mode<DataLayout::U16> () = result;
}

void predicated_select_live_value_escape ()
{
  vFloat condition = dst_reg[0].mode<DataLayout::F16b> ();
  vUInt on_true = dst_reg[32].mode<DataLayout::U16> ();
  vUInt on_false = dst_reg[64].mode<DataLayout::U16> ();
  vUInt result = on_false;
  v_if (condition != 0.0f)
    {
      result = on_true;
    }
  v_endif;
  dst_reg[0].mode<DataLayout::U16> () = result;
  l_reg[LRegs::LReg7] = on_true;
}
