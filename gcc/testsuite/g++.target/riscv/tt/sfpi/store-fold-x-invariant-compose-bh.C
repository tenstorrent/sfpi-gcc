// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_store_fold -fdump-tree-rvtt_invariant" }
// Check that the store-fold sink and the invariant immediate hoist both apply in a counted loop.
// { dg-final { scan-tree-dump "store-fold: sank post-region store" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "Hoisted invariant SFPU immediate" "rvtt_invariant" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel { constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;
__attribute__((noinline)) void
probe_sink_loop ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      vInt x = dst_reg[0];
      v_if (x >= 42) { x = x & 0x7ffff; } v_endif;
      dst_reg[0] = x;
      dst_reg++;
    }
}
