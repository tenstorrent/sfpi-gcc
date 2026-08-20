// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-int-abs -fdump-tree-rvtt_int_abs" }
// Near miss: the merge carries a value other than the compared one
// (w = v+7 outside, w = -v inside) -- a selection, not an absolute
// value.  Named refusal, bytes unchanged.
// { dg-final { scan-tree-dump "int-abs refused .int-abs-carried-value-mismatch" "rvtt_int_abs" } }
// { dg-final { scan-tree-dump "int-abs: folds=0" "rvtt_int_abs" } }
// { dg-final { scan-assembler-not "SFPABS" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
intabs_carried_other ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vInt v = sfpi::dst_reg[0];
      sfpi::vInt w = v + 7;
      v_if (v < 0) { w = sfpi::vInt(0) - v; }
      v_endif;
      sfpi::dst_reg[0] = w;
      sfpi::dst_reg++;
    }
}
