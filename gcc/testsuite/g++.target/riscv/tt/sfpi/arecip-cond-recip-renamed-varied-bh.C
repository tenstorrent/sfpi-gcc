// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// Renamed/varied twin: EXP feeding COND_RECIP (the documented
// "e^-x as 1/e^x without the sign dance" composition) inside a loop,
// different names -- both modes fire regardless of context.
// { dg-final { scan-assembler "SFPARECIP\tL\[0-7\], L\[0-7\], 2" } }
// { dg-final { scan-assembler "\\.ttinsn\t\[0-9\]+\t# SFPARECIP" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
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
