// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// Check that without -mtt-tensix-optimize-store-fold the predicated merge SFPMOV is kept.
// { dg-final { scan-assembler "SFPMOV" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
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
