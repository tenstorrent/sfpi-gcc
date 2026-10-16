// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -ffinite-math-only -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-leaf-ext -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_lut_select" }
// Check that a constant-register addend outside the LUT does not block preheader placement of the LUT coefficients.
// { dg-final { scan-tree-dump-times "formed fp32-3entry-sgn-update \\(mod0 0\\) from 3-range magnitude dispatch tree, boundaries 0x3f800000,0x40000000, slot leaves mul0,affine,const" 1 "rvtt_lut_select" } }
// The constant leaf's slot word is materialized as a FLOATB immediate so the
// loop needs no copy of L10 into the slot LREG.
// { dg-final { scan-tree-dump "slot creg value 0x3f800000 materialized as FLOATB immediate 0x3f80" "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-times "placed coefficient materialization in loop preheader" 5 "rvtt_lut_select" } }
// { dg-final { scan-tree-dump "placements=5" "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-not "lut-coefficient-pressure" "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }
// { dg-final { scan-assembler "SFPMAD\tL3, L3, L3, L10" } }
// { dg-final { scan-assembler-not "SFPMOV" } }

extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
lut_tree_creg_addend_places ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat x = sfpi::dst_reg[0];
      sfpi::vFloat a = sfpi::abs (x);
      sfpi::vFloat t = 1.0f;
      v_if (a < 1.0f) { t = a * 0.90625f; }
      v_elseif (a < 2.0f) { t = a * 0.09375f + 0.8125f; }
      v_endif;
      sfpi::dst_reg[0] = t * (-t) + 1.0f;
      sfpi::dst_reg++;
    }
}
