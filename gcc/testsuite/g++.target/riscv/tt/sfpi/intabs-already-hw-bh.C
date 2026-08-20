// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-int-abs -fdump-tree-rvtt_int_abs" }
// User code already using the hardware form (sfpi::abs on vInt) stays
// byte-identical with the flag on: nothing to fold, exactly the one
// SFPABS the source asked for.
// { dg-final { scan-tree-dump "int-abs: folds=0" "rvtt_int_abs" } }
// { dg-final { scan-assembler-times "SFPABS\tL\[0-7\], L\[0-7\], 0" 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
intabs_already_hw ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vInt v = sfpi::dst_reg[0].mode<sfpi::DataLayout::I32>();
      sfpi::dst_reg[0].mode<sfpi::DataLayout::M32>() = sfpi::abs(v);
      sfpi::dst_reg++;
    }
}
