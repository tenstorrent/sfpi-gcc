// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-int-not -fdump-tree-rvtt_int_not" }
// The equivalence proof ran against the shared TT_VERSION<=1 simulator
// arm both pinned oracles compile; Wormhole fires too.
// { dg-final { scan-tree-dump-times "int-not: folded all-ones subtract" 1 "rvtt_int_not" } }
// { dg-final { scan-assembler "SFPNOT" } }
// { dg-final { scan-assembler-not "SFPIADD" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
intnot_fire_wh ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vInt v = sfpi::dst_reg[0];
      sfpi::dst_reg[0] = sfpi::vInt(-1) - v;
      sfpi::dst_reg++;
    }
}
