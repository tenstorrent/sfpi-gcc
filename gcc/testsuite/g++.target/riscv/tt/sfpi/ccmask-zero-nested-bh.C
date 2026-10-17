// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// Check that a zeroing region nested in an enclosing v_if folds and the outer CC region stays.
// { dg-final { scan-tree-dump-times "ccmask: folded zeroing CC region" 1 "rvtt_ccmask" } }
// { dg-final { scan-assembler "SFPGT" } }
// { dg-final { scan-assembler "SFPLE" } }
// { dg-final { scan-assembler "SFPENCC" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
ccmask_nested (float t)
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat x = sfpi::dst_reg[0];
      sfpi::vFloat y = sfpi::dst_reg[1];
      sfpi::vFloat w = x + y;
      v_if (y >= 2.0f)
	{
	  v_if (x <= 0.0f) { w = 0.0f; }
	  v_endif;
	  w = w * 0.5f;
	}
      v_endif;
      sfpi::dst_reg[0] = w;
      sfpi::dst_reg++;
    }
}
