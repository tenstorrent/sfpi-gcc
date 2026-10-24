/* A counted Dst loop with a 3-range |x| dispatch tree, a sign restore from the input and an affine epilogue.  */

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
      sfpi::vFloat LUT_TREE_R = LUT_TREE_MAG * LUT_TREE_A2 + LUT_TREE_B2;
      v_if (LUT_TREE_MAG < 1.0f)
	{
	  LUT_TREE_R = LUT_TREE_MAG * LUT_TREE_A0 + LUT_TREE_B0;
	}
      v_elseif (LUT_TREE_MAG < 2.0f)
	{
	  LUT_TREE_R = LUT_TREE_MAG * LUT_TREE_A1 + LUT_TREE_B1;
	}
      v_endif;
      sfpi::dst_reg[0]
	= sfpi::copysgn (LUT_TREE_R, LUT_TREE_X) + LUT_TREE_EPI;
      sfpi::dst_reg++;
    }
}
