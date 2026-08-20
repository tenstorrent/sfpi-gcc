// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -ffinite-math-only -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-leaf-ext -fdump-tree-rvtt_lut_select" }
// Under the function's own -ffinite-math-only license the tail
// constant slot's divergence set (inf/NaN inputs, and nothing else --
// exhaustively enumerated) is exactly what the user excluded, so the
// natural saturating dispatch tree forms the full LUT: mul-only leaf,
// affine leaf, constant saturation leaf.  The predication scaffolding,
// leaf computations, and the now-internal abs all disappear.
// { dg-final { scan-tree-dump-times "formed fp32-3entry-sgn-update \\(mod0 0\\) from 3-range magnitude dispatch tree, boundaries 0x3f800000,0x40000000, slot leaves mul0,affine,const" 1 "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPCOMPC" } }
// { dg-final { scan-assembler-not "SFPABS" } }

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
lut_tree_const_tail_finite ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat x = sfpi::dst_reg[0];
      sfpi::vFloat mag = sfpi::abs (x);
      sfpi::vFloat t = 1.0f;
      v_if (mag < 1.0f) { t = mag * 0.90625f; }
      v_elseif (mag < 2.0f) { t = mag * 0.09375f + 0.8125f; }
      v_endif;
      sfpi::dst_reg[0] = t;
      sfpi::dst_reg++;
    }
}
