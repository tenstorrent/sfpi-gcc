// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// Check that approx_recip with RecipMode::IfNegative emits the raw SFPARECIP word with VB == VC.
// { dg-final { scan-assembler "\\.ttinsn\t\[0-9\]+\t# SFPARECIP" } }
// { dg-final { scan-assembler-not "\n\tSFPARECIP\tL\[0-7\], L\[0-7\], 1" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
arecip_cond_raw_word ()
{
  sfpi::vFloat x = sfpi::dst_reg[0];
  sfpi::dst_reg[0] = sfpi::approx_recip (x, sfpi::RecipMode::IfNegative);
}
