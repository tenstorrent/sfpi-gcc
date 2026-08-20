// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-leaf-ext -fdump-tree-rvtt_lut_select" }
// Below-arity extension: a two-range tree whose single boundary is the
// mode's FIRST architectural boundary (1.0) forms by duplicating the
// default leaf across the two adjacent slots it spans -- identical
// coefficients evaluate the identical fma, so the per-slot leaf proofs
// carry the argument.
// { dg-final { scan-tree-dump-times "formed fp32-3entry-sgn-update \\(mod0 0\\) from 2-range magnitude dispatch tree, boundaries 0x3f800000,0x40000000, slot leaves affine,affine,affine" 1 "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPABS" } }

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
lut_tree_two_ranges_forms ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat x = sfpi::dst_reg[0];
      sfpi::vFloat mag = sfpi::abs (x);
      sfpi::vFloat r = mag * 0.0913f + 0.4477f;
      v_if (mag < 1.0f) { r = mag * 0.1875f + 0.3125f; }
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
