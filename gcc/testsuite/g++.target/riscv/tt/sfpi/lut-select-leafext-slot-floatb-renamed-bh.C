// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -ffinite-math-only -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-leaf-ext -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_lut_select" }
// Check that a FLOATB-exact constant-register slot (LReg[9]) becomes a preheader FLOATB immediate.
// { dg-final { scan-tree-dump "formed fp32-3entry-sgn-update" "rvtt_lut_select" } }
// { dg-final { scan-tree-dump "slot creg value 0 materialized as FLOATB immediate 0" "rvtt_lut_select" } }
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
renamed_zero_leaf_slot_converts ()
{
  for (int row = 0; row < 8; ++row)
    {
      sfpi::vFloat inp = sfpi::dst_reg[0];
      sfpi::vFloat mag = sfpi::abs (inp);
      sfpi::vFloat acc = 0.0f;
      v_if (mag < 1.0f) { acc = mag * 0.78125f; }
      v_elseif (mag < 2.0f) { acc = mag * 0.15625f + 0.3125f; }
      v_endif;
      sfpi::dst_reg[0] = acc * (-acc) + 1.0f;
      sfpi::dst_reg++;
    }
}
