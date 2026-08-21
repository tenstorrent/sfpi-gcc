// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// Nested v_if: depth-2 candidate under two balanced narrowing regions
// still satisfies containment; hoists.
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 1 "rvtt_invariant" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
ccrestore_nested ()
{
  for (int ix = 0; ix < 32; ++ix)
    {
      sfpi::vFloat v = sfpi::dst_reg[0];
      sfpi::vFloat r = v;
      v_if (v <= 2.0f)
	{
	  v_if (v == 0.0f)
	    {
	      r = v * 6.25f;
	    }
	  v_endif;
	}
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
