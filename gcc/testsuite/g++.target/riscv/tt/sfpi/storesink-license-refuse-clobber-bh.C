// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -mtt-tensix-optimize-store-sink -fdump-tree-rvtt_store_fold" }
// Unrelated shape UNDER the license token: other Dst traffic between
// the carried load and the merge kills the address-identity premise --
// the license widens only the format-pair admission, every span/shape
// refusal of the sink applies to licensed candidates unchanged.
// Named refusal, bytes unchanged.
// { dg-final { scan-tree-dump "store-fold refused .store-fold-sink-span-clobbered" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "store-fold: forwarded=0 sunk=0 sunk-licensed=0" "rvtt_store_fold" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
storesink_clobber (float t)
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vFloat v = sfpi::dst_reg[0];
      sfpi::dst_reg[32] = v + 9.0f;
      sfpi::vFloat r = v;
      v_if (v <= t)
	{
	  r = 0.0f;
	}
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
