// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -ffinite-math-only -Wno-deprecated-declarations -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-leaf-ext -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_lut_select" }
// Near-miss twin: a slot operand read from a constant register
// whose architectural value is NOT on the audited record (LReg[8],
// 0.8373 -- and not FLOATB-exact either) must REFUSE the conversion
// by name and keep the historical operand byte shape: the affine
// leaf's additive coefficient below is exactly such a read, while the
// same kernel's 1.0 const leaf (LReg[10], FLOATB-exact) still
// converts -- the admission separates the two within one formation.
// { dg-final { scan-tree-dump "formed fp32-3entry-sgn-update" "rvtt_lut_select" } }
// { dg-final { scan-tree-dump "slot creg read kept as operand \\(lut-slot-coeff-value-unproven\\)" "rvtt_lut_select" } }
// { dg-final { scan-tree-dump "slot creg value 0x3f800000 materialized as FLOATB immediate 0x3f80" "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }
// The refused slot keeps its physical copy (the allocator's SFPMOV
// from LReg[8]); the converted 1.0 slot has none.
// { dg-final { scan-assembler-times "SFPMOV" 1 } }
// { dg-final { scan-assembler "SFPMAD\tL3, L3, L3, L10" } }

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
nonfloatb_creg_slot_keeps ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat x = sfpi::dst_reg[0];
      sfpi::vFloat a = sfpi::abs (x);
      sfpi::vFloat t = 1.0f;
      v_if (a < 1.0f) { t = a * 0.90625f; }
      v_elseif (a < 2.0f) { t = a * 0.09375f + sfpi::vConst0p8373; }
      v_endif;
      sfpi::dst_reg[0] = t * (-t) + 1.0f;
      sfpi::dst_reg++;
    }
}
