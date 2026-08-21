// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// v_else region: SFPCOMPC bounds the else arm's enable set by the
// region-entry save (SFPCOMPC.md) -- audited narrowing -- so
// candidates in BOTH arms hoist.
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 2 "rvtt_invariant" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
ccrestore_velse ()
{
  for (int ix = 0; ix < 32; ++ix)
    {
      sfpi::vFloat v = sfpi::dst_reg[0];
      sfpi::vFloat r = v;
      v_if (v == 0.0f)
	{
	  r = v * 2.75f;
	}
      v_else
	{
	  r = v * 4.5f;
	}
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
