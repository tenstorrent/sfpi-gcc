// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -fdump-tree-rvtt_lut_select" }
// Near miss: a sign restore exists in the loop but does NOT consume
// the tree's selected value (it re-signs the magnitude itself).  No
// fold: sign-update mode, explicit sign copy kept.
// { dg-final { scan-tree-dump-times "formed fp32-3entry-sgn-update \\(mod0 0\\)" 1 "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-not "folded trailing sign restore" "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }
// { dg-final { scan-assembler "SFPSETSGN" } }

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
lut_tree_sgn_not_result ()
{
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
      sfpi::dst_reg[0] = r + sfpi::copysgn (mag, x);
      sfpi::dst_reg++;
    }
}
