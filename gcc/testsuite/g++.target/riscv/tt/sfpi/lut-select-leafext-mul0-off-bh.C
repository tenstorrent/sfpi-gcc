// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -fdump-tree-rvtt_lut_select" }
// Default-off path: without -mtt-tensix-optimize-lut-select-leaf-ext
// the multiply-only leaf keeps the base matcher's refusal and the
// bytes stay at the predicated shape.
// { dg-final { scan-tree-dump "refused \\(lut-leaf-not-affine\\)" "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-not "formed " "rvtt_lut_select" } }
// { dg-final { scan-assembler-not "SFPLUTFP32" } }
// { dg-final { scan-assembler "SFPSETCC" } }

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
lut_tree_mul0_off ()
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
