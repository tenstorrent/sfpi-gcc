// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-leaf-ext -fdump-tree-rvtt_lut_select" }
// A constant saturation leaf in the TAIL slot is NOT bit-exact at
// default fp semantics: the slot computes fma(+0.0, |x|, C) = NaN for
// inf/NaN magnitudes ("usual IEEE754 rules", tt-isa-documentation
// SFPMAD.md) where the tree's untouched default lanes keep C -- the
// exhaustive enumeration exhibits exactly 2^23 mismatching inputs
// (first witness 0x7f800000).  Refuse by name; nothing is edited.
// (This is the natural tanh-derivative-LUT saturation shape.)
// { dg-final { scan-tree-dump "refused \\(lut-leaf-bitexact-unproven\\)" "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-not "formed " "rvtt_lut_select" } }
// { dg-final { scan-assembler-not "SFPLUTFP32" } }
// { dg-final { scan-assembler "SFPSETCC" } }

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
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
