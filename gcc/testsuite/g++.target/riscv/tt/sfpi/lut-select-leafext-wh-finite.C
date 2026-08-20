// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -ffinite-math-only -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-leaf-ext -fdump-tree-rvtt_lut_select" }
// Wormhole under -ffinite-math-only: every recorded WH divergence
// (negative-NaN bucketing, NaN payload leakage) lives on inputs the
// license excludes, and the finite-input enumerations passed -- the
// mul-only leaf forms.
// { dg-final { scan-tree-dump-times "formed fp32-3entry-sgn-update \\(mod0 0\\) from 3-range magnitude dispatch tree, boundaries 0x3f800000,0x40000000, slot leaves mul0,affine,affine" 1 "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
lut_tree_mul0_wh_finite ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat x = sfpi::dst_reg[0];
      sfpi::vFloat mag = sfpi::abs (x);
      sfpi::vFloat r = mag * 0.0913f + 0.4477f;
      v_if (mag < 1.0f) { r = mag * 0.1875f; }
      v_elseif (mag < 2.0f) { r = mag * 0.2651f + -0.0442f; }
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
