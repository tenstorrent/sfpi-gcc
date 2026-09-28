// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-fp16 -fdump-tree-rvtt_lut_select" }
// A compare boundary with an external consumer is not owned by the converted
// LUT region.  Formation may remove the other four boundary materializations,
// but the shared 0.5 value and its store must remain.
// { dg-final { scan-tree-dump-times "formed fp16-6entry-t2" 1 "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }
// { dg-final { scan-assembler-times "SFPLOADI" 13 } }
// { dg-final { scan-assembler-times "SFPSTORE" 2 } }

extern volatile unsigned long __instrn_buffer[];

namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}

#include <sfpi.h>

__attribute__((noinline)) void
lut_tree_fp16_shared_boundary ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat x = sfpi::dst_reg[0];
      sfpi::vFloat mag = sfpi::abs (x);
      sfpi::vFloat half = 0.5f;
      sfpi::vFloat r = mag * 0.0f + 0.499755859375f;
      v_if (mag < half)
	{
	  r = mag * 0.24609375f + (-0.00048828125f);
	}
      v_elseif (mag < 1.0f)
	{
	  r = mag * 0.216796875f + 0.0152587890625f;
	}
      v_elseif (mag < 1.5f)
	{
	  r = mag * 0.173828125f + 0.059814453125f;
	}
      v_elseif (mag < 2.0f)
	{
	  r = mag * 0.42578125f + (-0.318359375f);
	}
      v_elseif (mag < 4.0f)
	{
	  r = mag * 0.048583984375f + 0.30078125f;
	}
      v_endif;
      sfpi::dst_reg[0] = sfpi::copysgn (r, x);
      sfpi::dst_reg[1] = half;
      sfpi::dst_reg++;
    }
}
