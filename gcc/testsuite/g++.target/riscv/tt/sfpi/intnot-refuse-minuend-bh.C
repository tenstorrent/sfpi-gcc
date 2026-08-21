// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-int-not -fdump-tree-rvtt_int_not" }
// Near miss: (-2) - v is NOT a one's complement (the no-borrow
// argument needs the all-ones minuend).  No fire, bytes unchanged.
// { dg-final { scan-tree-dump "int-not: folds=0" "rvtt_int_not" } }
// { dg-final { scan-assembler-not "SFPNOT" } }
// { dg-final { scan-assembler "SFPIADD" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
intnot_minuend_other ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vInt v = sfpi::dst_reg[0];
      sfpi::dst_reg[0] = sfpi::vInt(-2) - v;
      sfpi::dst_reg++;
    }
}
