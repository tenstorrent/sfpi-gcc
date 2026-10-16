// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -fdump-tree-rvtt_store_fold" }
// Check that a merge whose only use is a store under the same mask forwards its source into the store.
// { dg-final { scan-tree-dump-times "store-fold: forwarded merge source" 1 "rvtt_store_fold" } }
// { dg-final { scan-assembler-not "SFPMOV" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
storefold_forward (float thresh)
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vFloat v = sfpi::dst_reg[0];
      sfpi::vFloat r = v;
      v_if (v < thresh)
	{
	  r = 0.0f;
	  sfpi::dst_reg[0] = r;
	}
      v_endif;
      sfpi::dst_reg++;
    }
}
