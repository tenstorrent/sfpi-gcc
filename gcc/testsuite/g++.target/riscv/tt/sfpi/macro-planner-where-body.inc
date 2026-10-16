namespace ckernel { volatile unsigned long *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

__attribute__((noinline)) void predicated_select_emittable ()
{
  /* Full-width config materialization needs all lanes enabled; this pair
     establishes that.  */
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
/* The Dst counter increment is kept after the macro launches.  */
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}
