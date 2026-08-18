// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// A zeroing region nested inside an enclosing v_if folds: the mask
// compare and the AND execute under the enclosing CC state, which is
// exactly the nested-predication semantics.  The enclosing region's
// own CC scaffolding stays.
// { dg-final { scan-tree-dump-times "ccmask: folded zeroing CC region" 1 "rvtt_ccmask" } }
// { dg-final { scan-assembler "SFPGT" } }
// { dg-final { scan-assembler "SFPSETCC" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
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
