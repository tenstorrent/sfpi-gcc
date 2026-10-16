// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// Check that EXP feeding COND_RECIP in a loop emits both SFPARECIP modes.
// { dg-final { scan-assembler "SFPARECIP\tL\[0-7\], L\[0-7\], 2" } }
// { dg-final { scan-assembler "\\.ttinsn\t\[0-9\]+\t# SFPARECIP" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
zz_softexp_row ()
{
  for (int t = 0; t < 4; ++t)
    {
      sfpi::vFloat inp = sfpi::dst_reg[0];
      sfpi::vFloat es = sfpi::approx_exp (inp);
      sfpi::vFloat full = sfpi::approx_recip (es, sfpi::RecipMode::IfNegative);
      sfpi::dst_reg[0] = full;
      sfpi::dst_reg++;
    }
}
