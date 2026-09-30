// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-int-abs -fdump-tree-rvtt_int_abs" }
// Near miss (one-bit-varied predicate): negation under `v > 0` is not
// an absolute value -- the effective enabled set {v > 0} reduces to
// neither proven set ({v < 0}, {v <= 0}), so the region's value
// function is not the proof's; named refusal, bytes unchanged.
// { dg-final { scan-tree-dump "int-abs refused .int-abs-region-shape" "rvtt_int_abs" } }
// { dg-final { scan-tree-dump "int-abs: folds=0" "rvtt_int_abs" } }
// { dg-final { scan-assembler-not "SFPABS" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
intabs_wrong_direction ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vInt v = sfpi::dst_reg[0];
      sfpi::vInt r = v;
      v_if (v > 0) { r = sfpi::vInt(0) - v; }
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
