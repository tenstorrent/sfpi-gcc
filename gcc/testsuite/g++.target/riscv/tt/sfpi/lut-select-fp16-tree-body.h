/* Counted Dst loop applying a 6-range affine dispatch on |x| over the FP16
   six-entry boundaries; LUT_TREE_TOP is the top boundary (3.0f = TABLE1,
   4.0f = TABLE2) and the coefficient macros vary within the LUT16 grid.  */

extern volatile unsigned long __instrn_buffer[];

namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}

#include <sfpi.h>

__attribute__((noinline)) void
LUT_TREE_FN ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat LUT_TREE_X = sfpi::dst_reg[0];
      sfpi::vFloat LUT_TREE_MAG = sfpi::abs (LUT_TREE_X);
      sfpi::vFloat LUT_TREE_R = LUT_TREE_MAG * LUT_TREE_A5 + LUT_TREE_B5;
      v_if (LUT_TREE_MAG < 0.5f)
	{
	  LUT_TREE_R = LUT_TREE_MAG * LUT_TREE_A0 + LUT_TREE_B0;
	}
      v_elseif (LUT_TREE_MAG < 1.0f)
	{
	  LUT_TREE_R = LUT_TREE_MAG * LUT_TREE_A1 + LUT_TREE_B1;
	}
      v_elseif (LUT_TREE_MAG < 1.5f)
	{
	  LUT_TREE_R = LUT_TREE_MAG * LUT_TREE_A2 + LUT_TREE_B2;
	}
      v_elseif (LUT_TREE_MAG < 2.0f)
	{
	  LUT_TREE_R = LUT_TREE_MAG * LUT_TREE_A3 + LUT_TREE_B3;
	}
      v_elseif (LUT_TREE_MAG < LUT_TREE_TOP)
	{
	  LUT_TREE_R = LUT_TREE_MAG * LUT_TREE_A4 + LUT_TREE_B4;
	}
      v_endif;
#ifdef LUT_TREE_SETSGN
      /* Stored directly: a reassignment through the vFloat variable
	 would wrap the sign copy in a live-value assign and the tree
	 result would no longer have the single consumer the fold
	 requires.  */
      sfpi::dst_reg[0] = sfpi::copysgn (LUT_TREE_R, LUT_TREE_X);
#else
      sfpi::dst_reg[0] = LUT_TREE_R;
#endif
      sfpi::dst_reg++;
    }
}
