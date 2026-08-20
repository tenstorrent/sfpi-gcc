// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-int-abs -fdump-tree-rvtt_int_abs" }
// Genericity: renamed values, a different Dst offset, surrounding
// arithmetic, and two unrelated shapes in one function -- the fold is
// keyed to the dataflow shape, never to names or context; both regions
// fold.
// { dg-final { scan-tree-dump-times "int-abs: folded negate-select CC region" 2 "rvtt_int_abs" } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
intabs_two_shapes ()
{
  for (int ix = 0; ix < 4; ++ix)
    {
      const sfpi::vInt alpha = sfpi::dst_reg[2];
      sfpi::vInt beta = alpha;
      v_if (alpha < 0) { beta = sfpi::vInt(0) - alpha; }
      v_endif;
      sfpi::vInt gamma = beta + 21;
      const sfpi::vInt delta = sfpi::dst_reg[6];
      sfpi::vInt eps = delta;
      v_if (delta < 0) { eps = sfpi::vInt(0) - delta; }
      v_endif;
      sfpi::dst_reg[2] = gamma;
      sfpi::dst_reg[6] = eps;
      sfpi::dst_reg++;
    }
}
