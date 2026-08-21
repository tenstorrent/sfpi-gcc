// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-int-not -fdump-tree-rvtt_int_not" }
// Genericity: renamed values, the unsigned all-ones spelling, a
// different Dst offset, surrounding arithmetic, and two unrelated
// shapes in one function -- the fold is keyed to the value function
// (all-ones minus x), never to names, types, or context.
// { dg-final { scan-tree-dump-times "int-not: folded all-ones subtract" 2 "rvtt_int_not" } }
// (the surrounding `delta + 3` legitimately emits its own SFPIADD; the
// two complement subtracts are gone by the dump count above)
// { dg-final { scan-assembler-times "SFPNOT" 2 } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
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
