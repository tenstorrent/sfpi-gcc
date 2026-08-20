// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-int-abs -fdump-tree-rvtt_int_abs" }
// Near miss (one varied constant): the predicated subtract is 1 - v,
// not 0 - v.  The region is not an absolute value; the fold refuses by
// name and the CC lowering stays byte-identical.
// { dg-final { scan-tree-dump "int-abs refused .int-abs-minuend-not-zero" "rvtt_int_abs" } }
// { dg-final { scan-tree-dump "int-abs: folds=0" "rvtt_int_abs" } }
// { dg-final { scan-assembler-not "SFPABS" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
intabs_minuend_one ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vInt v = sfpi::dst_reg[0];
      sfpi::vInt r = v;
      v_if (v < 0) { r = sfpi::vInt(1) - v; }
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
