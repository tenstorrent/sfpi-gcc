// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-leaf-ext -fdump-tree-rvtt_lut_select" }
// Check that a constant tail leaf refuses without -ffinite-math-only, since inf/NaN magnitudes differ (lut-leaf-bitexact-unproven).
// { dg-final { scan-tree-dump "refused \\(lut-leaf-bitexact-unproven\\)" "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-not "formed " "rvtt_lut_select" } }
// { dg-final { scan-assembler-not "SFPLUTFP32" } }
// { dg-final { scan-assembler "SFPGT" } }
// { dg-final { scan-assembler "SFPENCC" } }

extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
lut_tree_const_tail ()
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
