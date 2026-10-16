// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -mtt-tensix-optimize-store-sink -fdump-tree-rvtt_store_fold" }
// Check that with -mtt-tensix-optimize-store-sink an SRCB-format pair refuses the sink (store-fold-sink-format-unproven).
// { dg-final { scan-tree-dump "store-fold refused .store-fold-sink-format-unproven" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "store-fold: forwarded=0 sunk=0 sunk-licensed=0" "rvtt_store_fold" } }
// { dg-final { scan-assembler "SFPMOV" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
storesink_refuse_srcb (float t)
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vFloat v = sfpi::dst_reg[0];
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
