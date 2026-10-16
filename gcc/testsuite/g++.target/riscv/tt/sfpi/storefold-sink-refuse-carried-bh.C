// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -fdump-tree-rvtt_store_fold" }
// Check that a merge carrying a computed value rather than the load refuses the sink (store-fold-sink-carried-not-load).
// { dg-final { scan-tree-dump "store-fold refused .store-fold-sink-carried-not-load" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "store-fold: forwarded=0 sunk=0" "rvtt_store_fold" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
storefold_carried_other (int lim)
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vInt v = sfpi::dst_reg[0];
      sfpi::vInt r = v + 5;
      v_if (v < lim)
	{
	  r = sfpi::vInt(0);
	}
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
