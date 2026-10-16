// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-int-not -fdump-tree-rvtt_int_not" }
// Check that a subtract from a runtime minuend is not folded to SFPNOT.
// { dg-final { scan-tree-dump "int-not: folds=0" "rvtt_int_not" } }
// { dg-final { scan-assembler-not "SFPNOT" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
intnot_varimm (int k)
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vInt v = sfpi::dst_reg[0];
      sfpi::dst_reg[0] = sfpi::vInt(k) - v;
      sfpi::dst_reg++;
    }
}
