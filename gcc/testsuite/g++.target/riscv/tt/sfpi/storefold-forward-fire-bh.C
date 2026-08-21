// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -fdump-tree-rvtt_store_fold" }
// S1 same-mask forwarding: a merge whose only consumer is a store under
// the SAME mask forwards its source into the store; the predicated
// SFPMOV the merge would expand to disappears.  Dataflow proof only --
// the store writes exactly the mask lanes in both forms.
// { dg-final { scan-tree-dump-times "store-fold: forwarded merge source" 1 "rvtt_store_fold" } }
// { dg-final { scan-assembler-not "SFPMOV" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
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
