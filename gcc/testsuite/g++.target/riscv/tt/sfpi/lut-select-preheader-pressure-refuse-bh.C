// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -fdump-tree-rvtt_lut_select" }
// LREG-budget near miss: one extra vector value lives across the loop
// (seven values pinned across the body), so keeping the six
// coefficients live as well would exceed the eight-LREG file at the
// row's peak.  Placement refuses transactionally -- the LUT (with the
// folded sign restore) still forms, but every coefficient
// materialization stays in the loop exactly as in the formation-only
// shape.
// { dg-final { scan-tree-dump-times "formed fp32-3entry-sgn-retain \\(mod0 0x4\\)" 1 "rvtt_lut_select" } }
// { dg-final { scan-tree-dump "coefficients kept in loop \\(lut-coefficient-pressure\\)" "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-not "placed coefficient materialization" "rvtt_lut_select" } }
// { dg-final { scan-tree-dump "placements=0" "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
lut_tree_pressure ()
{
  sfpi::vFloat held = sfpi::l_reg[sfpi::LRegs::LReg7];
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat x = sfpi::dst_reg[0];
      sfpi::vFloat mag = sfpi::abs (x);
      sfpi::vFloat r = mag * 0.0375f + 0.3058f;
      v_if (mag < 1.0f)
	{
	  r = mag * 0.2452f + -0.0005f;
	}
      v_elseif (mag < 2.0f)
	{
	  r = mag * 0.1497f + 0.0814f;
	}
      v_endif;
      sfpi::dst_reg[0] = sfpi::copysgn (r, x) + 0.5f;
      sfpi::dst_reg++;
    }
  sfpi::l_reg[sfpi::LRegs::LReg7] = held;
}
