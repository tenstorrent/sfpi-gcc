// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-int-not -fdump-tree-rvtt_int_not" }
// Check that two all-ones subtracts with renamed values and the unsigned spelling both fold.
// { dg-final { scan-tree-dump-times "int-not: folded all-ones subtract" 2 "rvtt_int_not" } }
// The surrounding delta + 3 emits its own SFPIADD.
// { dg-final { scan-assembler-times "SFPNOT" 2 } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
intnot_two_shapes ()
{
  for (int ix = 0; ix < 4; ++ix)
    {
      const sfpi::vUInt alpha = sfpi::dst_reg[2];
      const sfpi::vUInt beta = sfpi::vUInt(0xFFFFFFFFu) - alpha;
      const sfpi::vInt gamma = sfpi::dst_reg[6];
      sfpi::vInt delta = sfpi::vInt(-1) - gamma;
      delta = delta + 3;
      sfpi::dst_reg[2] = beta;
      sfpi::dst_reg[6] = delta;
      sfpi::dst_reg++;
    }
}
