// { dg-options "-mcpu=tt-bh-tensix -O3 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// A pressure-limited short CC loop can be fully unrolled after the early
// invariant pass, multiplying live ranges beyond its single-body model.
// { dg-final { scan-tree-dump "cc-restore-unroll-pressure-unmodeled .pressure-limited short constant trip count." "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
ccrestore_short_trip_refuse ()
{
  for (int ix = 0; ix < 4; ++ix)
    {
      sfpi::vFloat v = sfpi::dst_reg[0];
      sfpi::vFloat r = v * 0.6931471805f;
      r = r * 1.125f + v;
      r = r * 1.25f + v;
      r = r * 1.375f + v;
      r = r * 1.5f + v;
      r = r * 1.625f + v;
      r = r * 1.75f + v;
      r = r * 1.875f + v;
      r = r * 2.125f + v;
      v_if (v == 0.0f)
	{
	  r = sfpi::vFloat (-88.72284f) * v;
	}
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
