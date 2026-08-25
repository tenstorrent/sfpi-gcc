/* Parameterized kernel-shaped 6-range magnitude dispatch tree: a
   counted Dst loop whose body loads a row, computes a range-dispatched
   affine function of |x| over the architectural FP16 six-entry
   boundaries, and stores the row back.  The coefficient macros are
   free values within the LUT16-exact grid: the selection decision must
   be identical under any renaming of LUT_TREE_* and under any
   LUT16-exact coefficient values (the pass may key only on the
   dataflow shape, the architectural range boundaries, and the
   coefficient encodability proofs).  LUT_TREE_TOP is the top
   architectural boundary (3.0f = TABLE1, 4.0f = TABLE2).  */

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
      LUT_TREE_R = sfpi::setsgn (LUT_TREE_R, LUT_TREE_X);
#endif
      sfpi::dst_reg[0] = LUT_TREE_R;
      sfpi::dst_reg++;
    }
}
