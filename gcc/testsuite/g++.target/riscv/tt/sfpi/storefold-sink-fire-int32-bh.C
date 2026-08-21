// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -fdump-tree-rvtt_store_fold" }
// S2 sink on the raw INT32 pair: the post-region all-lanes store of the
// merge sinks into the region as a predicated store of the source; the
// enabled-complement lanes' write-back is a proven Dst no-op --
// tt/proofs/store-sink-roundtrip/ pair (4,4), EQUAL over 2^32.
// { dg-final { scan-tree-dump-times "store-fold: sank post-region store" 1 "rvtt_store_fold" } }
// { dg-final { scan-assembler-not "SFPMOV" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
storefold_sink_int (int lim)
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
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
