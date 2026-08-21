// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// Merge-fed candidate (threshold-fresh shape): the in-region
// assignment to a live-in vector is a plain all-constant load feeding
// sfpassign_lv.  The load hoists under the containment refinement; the
// merge KEEPS its position and mask, lowering to the lane-predicated
// SFPMOV merge (rvtt.md *rvtt_sfpassign_lv_int) now that the load no
// longer directly precedes it.
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 1 "rvtt_invariant" } }
// { dg-final { scan-assembler "SFPMOV.*# LV:L" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
ccrestore_merge ()
{
  for (int ix = 0; ix < 32; ++ix)
    {
      sfpi::vFloat v = sfpi::dst_reg[0];
      v_if (v <= 0.5f)
	{
	  v = 1.5f;
	}
      v_endif;
      sfpi::dst_reg[0] = v;
      sfpi::dst_reg++;
    }
}
