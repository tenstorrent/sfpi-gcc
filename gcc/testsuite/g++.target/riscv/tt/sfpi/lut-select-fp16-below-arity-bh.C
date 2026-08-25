// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-fp16 -mtt-tensix-optimize-lut-select-leaf-ext -fdump-tree-rvtt_lut_select" }
// Below-arity generalization: a 3-leaf tree on the {0.5, 1.0} boundary
// subset forms the FP16 six-entry TABLE1 by duplicating its tail leaf
// across the four slots it spans (slot duplication is a leaf-ext
// capability).
// { dg-final { scan-tree-dump-times "formed fp16-6entry-t1-sgn-update \\(mod0 0x2\\) from 3-range magnitude dispatch tree, boundaries 0x3f000000,0x3f800000,0x3fc00000,0x40000000,0x40400000, slot leaves affine,affine,affine,affine,affine,affine" 1 "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
lut_tree_fp16_below_arity ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat x = sfpi::dst_reg[0];
      sfpi::vFloat mag = sfpi::abs (x);
      sfpi::vFloat r = mag * 0.0625f + 0.90625f;
      v_if (mag < 0.5f) { r = mag * 0.9375f + 0.015625f; }
      v_elseif (mag < 1.0f) { r = mag * 0.75f + 0.0859375f; }
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
  }
