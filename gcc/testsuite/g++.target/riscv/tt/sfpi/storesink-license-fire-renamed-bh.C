// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -mtt-tensix-optimize-store-sink -fdump-tree-rvtt_store_fold" }
// Renamed-equivalent twin of the licensed fire (charter adversarial
// pair): every identifier renamed, hardshrink-flavored condition (abs
// compare) -- the admission keys on the SHAPE (predicated value merge
// feeding a post-region all-lanes store on a float pair), never on
// names, so the licensed sink fires identically.
// { dg-final { scan-tree-dump-times "store-fold: licensed sink" 1 "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "store-fold: forwarded=0 sunk=0 sunk-licensed=1" "rvtt_store_fold" } }
// { dg-final { scan-assembler-not "SFPMOV" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
zq_wobble_kernel (float lambda_knee)
{
  for (int trip_ctr = 0; trip_ctr < 8; ++trip_ctr)
    {
      const sfpi::vFloat datum = sfpi::dst_reg[0];
      sfpi::vFloat outv = datum;
      v_if (sfpi::abs (datum) <= lambda_knee)
	{
	  outv = 0.0f;
	}
      v_endif;
      sfpi::dst_reg[0] = outv;
      sfpi::dst_reg++;
    }
}
