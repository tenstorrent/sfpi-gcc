/* ccmask-body.h zeroing CC frame with scalar control flow inside the region,
   so the frame spans several basic blocks.

   Hooks:
     CCL_REGION_BODY(y, s, vp)  the region body (the zeroing assign
				plus the scalar control flow variant
				under test)  */

extern volatile unsigned long __instrn_buffer[];

namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}

#include <sfpi.h>

#ifndef CCL_COND
#define CCL_COND(x) ((x) <= 0.0f)
#endif

__attribute__((noinline)) void
CCL_FN (int CCL_S, volatile int *CCL_VP)
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat CCL_X = sfpi::dst_reg[0];
      sfpi::vFloat CCL_Y = CCL_X * 0.4375f + 1.5f;
      v_if (CCL_COND (CCL_X))
	{
	  CCL_REGION_BODY (CCL_Y, CCL_S, CCL_VP);
	}
      v_endif;
      sfpi::dst_reg[0] = CCL_Y;
      sfpi::dst_reg++;
    }
}
