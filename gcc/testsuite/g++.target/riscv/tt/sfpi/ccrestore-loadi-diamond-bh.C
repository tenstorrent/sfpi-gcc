// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// Multi-block region: a scalar diamond inside the v_if.  The depth
// dataflow joins consistently (both arms carry depth 1), so the
// restore proof holds and the depth-0 candidate hoists; the arm
// candidates stay by the executes-every-entered-iteration speculation
// guard (their blocks do not dominate the latch).
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 1 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "cc-restore-unstructured" "rvtt_invariant" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
ccrestore_diamond (int k)
{
  for (int d = 0; d < 32; ++d)
    {
      sfpi::vFloat v = sfpi::dst_reg[0];
      sfpi::vFloat r = v * 1.1328125f;
      v_if (v == 0.0f)
	{
	  if (k)
	    r = v * 2.125f;
	  else
	    r = v * 3.25f;
	}
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
