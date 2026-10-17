// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -mtt-tensix-optimize-store-sink -fdump-tree-rvtt_store_fold" }
// Check that the licensed store sink fires with a different compare, merge constant and trip count.
// { dg-final { scan-tree-dump-times "store-fold: licensed sink" 1 "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "store-fold: forwarded=0 sunk=0 sunk-licensed=1" "rvtt_store_fold" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
storesink_varied (void)
{
  for (int ix = 0; ix < 32; ++ix)
    {
      const sfpi::vFloat v = sfpi::dst_reg[0].mode<sfpi::DataLayout::F16b> ();
      sfpi::vFloat r = v;
      v_if (v >= 1.5f)
	{
	  r = 6.5f;
	}
      v_endif;
      sfpi::dst_reg[0].mode<sfpi::DataLayout::F16b> () = r;
      sfpi::dst_reg++;
    }
}
