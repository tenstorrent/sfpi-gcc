// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// A CC-carrying loop under an explicit unroll request multiplies its
// in-loop live ranges after this pass; the single-body pressure walk
// models none of that overlap, and a miss becomes the post-allocation
// lreg-pressure-exceeded user error (corpus witness: the
// pragma-unroll-8 snake-beta body).  Refuse hoisting by name.
// { dg-final { scan-tree-dump "cc-restore-unroll-pressure-unmodeled" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
ccrestore_unroll_refuse ()
{
#pragma GCC unroll 8
  for (int ix = 0; ix < 32; ++ix)
    {
      sfpi::vFloat v = sfpi::dst_reg[0];
      sfpi::vFloat r = v * 0.6931471805f;
      v_if (v == 0.0f)
	{
	  r = sfpi::vFloat (-88.72284f) * v;
	}
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
