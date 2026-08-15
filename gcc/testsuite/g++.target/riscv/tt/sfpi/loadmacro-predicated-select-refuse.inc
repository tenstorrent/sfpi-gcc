namespace ckernel { unsigned *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

#if SELECT_REFUSE_CALL
extern void loadmacro_config_owner ();
#endif

__attribute__((noinline)) void predicated_select_refused ()
{
#if SELECT_REFUSE_CALL
  loadmacro_config_owner ();
#endif
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
#if SELECT_INCREMENT == 1
  __builtin_rvtt_ttincrwc (0, 1, 0, 0);
#elif SELECT_INCREMENT == 2
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
#endif
}
