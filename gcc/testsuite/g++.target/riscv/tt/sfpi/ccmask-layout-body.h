/* Stage-B layout-generality test shape (FABLE_GOES_BURR item #14
   stage B): the same single-zero-assign CC frame as ccmask-body.h, but
   with scalar control flow INSIDE the region -- the frame's statements
   span several basic blocks, so the stage-A linear statement machine
   cannot match it while the CC-region tree proves the identical frame
   structure.  The CCL_* macros are free names and values: the stage-B
   admission must be identical under renaming and under any surrounding
   scalar work (it keys only on the tree's frame facts, the proven
   execution order, and the +0.0 boundary).

   Hooks:
     CCL_REGION_BODY(y, s, vp)  the region body (the zeroing assign
				plus the scalar control flow variant
				under test)  */

extern volatile unsigned __instrn_buffer[];

namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
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
