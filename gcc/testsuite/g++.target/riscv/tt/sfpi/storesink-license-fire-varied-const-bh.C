// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -mtt-tensix-optimize-store-sink -fdump-tree-rvtt_store_fold" }
// Varied-constants twin of the licensed fire (charter adversarial
// pair): different compare direction, non-zero merge constant, longer
// trip count -- no constant participates in the admission, so the
// licensed sink fires identically.
// { dg-final { scan-tree-dump-times "store-fold: licensed sink" 1 "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "store-fold: forwarded=0 sunk=0 sunk-licensed=1" "rvtt_store_fold" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
storesink_varied (void)
{
  for (int ix = 0; ix < 32; ++ix)
    {
      const sfpi::vFloat v = sfpi::dst_reg[0];
      sfpi::vFloat r = v;
      v_if (v >= 1.5f)
	{
	  r = 6.5f;
	}
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
