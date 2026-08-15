// { dg-options "-mcpu=tt-bh-tensix -O3 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-emit-loadmacro -fno-unroll-loops" }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }

namespace ckernel { unsigned *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

__attribute__((noinline)) void predicated_select_zero_trip (unsigned rows)
{
  while (rows != 0)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfppopc (0);
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
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
      --rows;
    }
}
