// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// Near miss: an integer compare keeps the CC lowering (the mask
// equivalence proof covers the float order test against +0.0 only);
// no fold fires.
// { dg-final { scan-tree-dump "ccmask: folds=0" "rvtt_ccmask" } }
// { dg-final { scan-assembler-not "SFPGT" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
ccmask_int_cmp ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vInt x = sfpi::exexp (sfpi::dst_reg[0], sfpi::ExponentMode::Biased);
      sfpi::vInt w = x + 3;
      v_if (x <= 0) { w = 0; }
      v_endif;
      sfpi::dst_reg[0] = sfpi::setexp (sfpi::dst_reg[0], w);
      sfpi::dst_reg++;
    }
}
