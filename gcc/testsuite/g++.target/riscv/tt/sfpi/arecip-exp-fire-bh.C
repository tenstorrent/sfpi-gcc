// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// GS-3 unlock: sfpi::approx_exp lowers to the single SFPARECIP EXP-mode
// word (Mod1 = 2; the functional model reads no VB, so the mnemonic's
// forced-zero imm12 is exact).  Semantic-source builtin: no flag gate.
// { dg-final { scan-assembler-times "SFPARECIP\tL\[0-7\], L\[0-7\], 2" 1 } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
arecip_exp_fire ()
{
  sfpi::vFloat x = sfpi::dst_reg[0];
  sfpi::dst_reg[0] = sfpi::approx_exp (x);
}
