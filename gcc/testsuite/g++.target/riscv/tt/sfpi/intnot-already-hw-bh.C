// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-int-not -fdump-tree-rvtt_int_not" }
// The operator~ spelling already lowers to SFPNOT in the wrapper; the
// pass has nothing to do and must not disturb it.
// { dg-final { scan-tree-dump "int-not: folds=0" "rvtt_int_not" } }
// { dg-final { scan-assembler "SFPNOT" } }
// { dg-final { scan-assembler-not "SFPIADD" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
intnot_already_hw ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vInt v = sfpi::dst_reg[0];
      sfpi::dst_reg[0] = ~v;
      sfpi::dst_reg++;
    }
}
