// { dg-options "-mcpu=tt-bh-tensix -O3 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-emit-loadmacro" }
// { dg-final { scan-assembler-times "\\.ttinsn" 3 } }
// { dg-final { scan-assembler-times "SFPCONFIG" 7 } }
// { dg-final { scan-assembler-times {SFPCONFIG\t15} 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPSTORE" } }

namespace ckernel { unsigned *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

__attribute__((noinline)) void predicated_select_disjoint_config ()
{
  vUInt unrelated = 0u;
  __builtin_rvtt_sfpwriteconfig_v (unrelated.get (), 15);
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
}
