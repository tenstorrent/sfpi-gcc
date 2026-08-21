// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// Default off: without -mtt-tensix-optimize-store-fold the in-region
// store keeps its merge (the predicated SFPMOV) byte-identically.
// { dg-final { scan-assembler "SFPMOV" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
storefold_default_off (float thresh)
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vFloat v = sfpi::dst_reg[0];
      sfpi::vFloat r = v;
      v_if (v < thresh)
	{
	  r = 0.0f;
	  sfpi::dst_reg[0] = r;
	}
      v_endif;
      sfpi::dst_reg++;
    }
}
