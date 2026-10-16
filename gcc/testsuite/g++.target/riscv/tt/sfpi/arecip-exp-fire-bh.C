// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// Check that approx_exp lowers to the plain SFPARECIP EXP-mode mnemonic (Mod1 = 2).
// { dg-final { scan-assembler-times "SFPARECIP\tL\[0-7\], L\[0-7\], 2" 1 } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
arecip_exp_fire ()
{
  sfpi::vFloat x = sfpi::dst_reg[0];
  sfpi::dst_reg[0] = sfpi::approx_exp (x);
}
