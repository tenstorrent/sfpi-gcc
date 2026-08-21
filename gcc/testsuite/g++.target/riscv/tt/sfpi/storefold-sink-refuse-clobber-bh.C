// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -fdump-tree-rvtt_store_fold" }
// Near miss: other Dst traffic sits between the carried load and the
// merge, so the address-identity/no-intervening-write premise is gone.
// Named refusal, bytes unchanged.
// { dg-final { scan-tree-dump "store-fold refused .store-fold-sink-span-clobbered" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "store-fold: forwarded=0 sunk=0" "rvtt_store_fold" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
storefold_clobber (int lim)
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vInt v = sfpi::dst_reg[0];
      sfpi::dst_reg[32] = v + 9;
      sfpi::vInt r = v;
      v_if (v < lim)
	{
	  r = sfpi::vInt(0);
	}
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
