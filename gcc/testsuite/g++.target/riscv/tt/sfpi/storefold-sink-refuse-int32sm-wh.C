// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -fdump-tree-rvtt_store_fold" }
// Wormhole's integer Dst pair is INT32_SM (mod0=12): the load and store
// CONVERT between the sign-magnitude Dst encoding and two's complement,
// and the round trip is NOT the identity (-0 normalizes:
// tt/proofs/store-sink-roundtrip/ WH (12,12) row, NOT-EQUAL).  The sink
// refuses by name and the merge stays.
// { dg-final { scan-tree-dump "store-fold refused .store-fold-sink-format-canonicalizing" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "store-fold: forwarded=0 sunk=0" "rvtt_store_fold" } }
// { dg-final { scan-assembler "SFPMOV" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
storefold_sink_int_wh (int lim)
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
