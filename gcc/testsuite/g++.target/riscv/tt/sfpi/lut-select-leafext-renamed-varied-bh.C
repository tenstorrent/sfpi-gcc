// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -ffinite-math-only -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-leaf-ext -fdump-tree-rvtt_lut_select" }
// Rename/value invariance: the same three-class shape as the
// const-tail-finite twin under different names and different
// coefficient/constant values must make the identical decision (the
// matcher keys only on dataflow shape, the architectural boundaries,
// and the certified value CLASS of the constant -- 2.5 is as normal
// as 1.0).
// { dg-final { scan-tree-dump-times "formed fp32-3entry-sgn-update \\(mod0 0\\) from 3-range magnitude dispatch tree, boundaries 0x3f800000,0x40000000, slot leaves mul0,affine,const" 1 "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
renamed_varied_kernel ()
{
  for (int qq = 0; qq < 8; ++qq)
    {
      sfpi::vFloat input = sfpi::dst_reg[0];
      sfpi::vFloat magnitude = sfpi::abs (input);
      sfpi::vFloat out = 2.5f;
      v_if (magnitude < 1.0f) { out = magnitude * 0.71875f; }
      v_elseif (magnitude < 2.0f) { out = magnitude * 0.11f + 0.6f; }
      v_endif;
      sfpi::dst_reg[0] = out;
      sfpi::dst_reg++;
    }
}
