// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -fdump-tree-rvtt_store_fold" }
// Near miss: the store targets a DIFFERENT Dst address than the carried
// load, so the complement lanes' write-back is not a round trip at all.
// Named refusal, bytes unchanged.
// { dg-final { scan-tree-dump "store-fold refused .store-fold-sink-address-mismatch" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "store-fold: forwarded=0 sunk=0" "rvtt_store_fold" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
storefold_addr_other (int lim)
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vInt v = sfpi::dst_reg[0];
      sfpi::vInt r = v;
      v_if (v < lim)
	{
	  r = sfpi::vInt(0);
	}
      v_endif;
      sfpi::dst_reg[32] = r;
      sfpi::dst_reg++;
    }
}
