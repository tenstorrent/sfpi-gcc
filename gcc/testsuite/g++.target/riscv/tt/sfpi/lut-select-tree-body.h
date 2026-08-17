/* Parameterized kernel-shaped 3-range magnitude dispatch tree: a
   counted Dst loop whose body loads a row, computes a range-dispatched
   affine function of |x|, and stores the row back.  The coefficient
   macros are free values: the LUT selection decision must be identical
   under any renaming of LUT_TREE_* and any coefficient values (the
   pass may key only on the dataflow shape and the architectural range
   boundaries).  */

extern volatile unsigned __instrn_buffer[];

namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
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
      sfpi::dst_reg[0] = LUT_TREE_R;
      sfpi::dst_reg++;
    }
}
