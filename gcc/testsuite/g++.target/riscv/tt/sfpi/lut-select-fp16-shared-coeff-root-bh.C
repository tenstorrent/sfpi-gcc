// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-fp16 -fdump-tree-rvtt_lut_select" }
// A chained coefficient root is shared with an observable second store.  LUT
// formation may delete the coefficient tail and tree, but its zero-use recheck
// must preserve the shared root and the store that consumes it.
// { dg-final { scan-tree-dump-times "formed fp16-6entry-t1-sgn-update" 1 "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }
// Six packed coefficient words take twelve loads; the shared chained root is
// the thirteenth.  Its tail is dead and must not remain.
// { dg-final { scan-assembler-times "SFPLOADI" 13 } }
// { dg-final { scan-assembler-times "SFPSTORE" 2 } }

extern volatile unsigned long __instrn_buffer[];

namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}

#include <sfpi.h>
#undef __builtin_rvtt_sfploadi

__attribute__((noinline)) void
lut_tree_fp16_shared_coeff_root ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat x = sfpi::dst_reg[0];
      sfpi::vFloat mag = sfpi::abs (x);
      auto root = __builtin_rvtt_sfploadi (nullptr, 0xe000, 0, 0, 2);
      auto tail
	= __builtin_rvtt_sfploadi_lv (nullptr, root, 0xb817, 0, 0, 8);
      sfpi::vFloat shared_coeff (tail);
      sfpi::vFloat r = mag * 0x1p-1f + shared_coeff;
      v_if (mag < 0.5f)
	{
	  r = mag * 0x1.58p-3f + (-0x1.2fcp-15f);
	}
      v_elseif (mag < 1.0f)
	{
	  r = mag * 0x1.f68p-2f + (-0x1.404p-3f);
	}
      v_elseif (mag < 1.5f)
	{
	  r = mag * 0x1.3cp-1f + (-0x1.1cp-2f);
	}
      v_elseif (mag < 2.0f)
	{
	  r = mag * 0x1.384p-1f + (-0x1.0ep-2f);
	}
      v_elseif (mag < 3.0f)
	{
	  r = mag * 0x1.158p-1f + (-0x1p-3f);
	}
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg[1] = sfpi::vFloat (root);
      sfpi::dst_reg++;
    }
}
