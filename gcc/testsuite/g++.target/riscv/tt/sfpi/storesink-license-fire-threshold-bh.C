// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -mtt-tensix-optimize-store-sink -fdump-tree-rvtt_store_fold" }
// The LICENSED S2 sink (owner ratification 2026-08-26) on the
// threshold-class float shape: with BOTH -mtt-tensix-optimize-store-fold
// and the -mtt-tensix-optimize-store-sink license token, the post-region
// all-lanes store of the predicated value merge sinks into the region as
// a predicated store -- the merge word disappears and the enabled-
// complement lanes keep their original Dst bits (the value change the
// license admits: the write-back would have flushed their denormals,
// tt/proofs/store-sink-roundtrip NOT-EQUAL float rows, all mismatches
// denormal-class).
// { dg-final { scan-tree-dump-times "store-fold: licensed sink" 1 "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "store-fold: forwarded=0 sunk=0 sunk-licensed=1" "rvtt_store_fold" } }
// { dg-final { scan-assembler-not "SFPMOV" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
storesink_threshold (float t)
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
