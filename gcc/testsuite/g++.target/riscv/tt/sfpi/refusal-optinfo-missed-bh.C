// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -fopt-info-missed -fdump-tree-rvtt_store_fold" }
// Check that a store-fold refusal is reported through -fopt-info-missed and not in the pass dump.
// { dg-prune-output "missed: " }
// { dg-final { scan-tree-dump "store-fold refused .store-fold-sink-format-canonicalizing" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump-not "tt-refusal" "rvtt_store_fold" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void optinfo_float_shrink (float lam) // { dg-missed "tt-refusal: store-fold-sink-format-canonicalizing \\\[rvtt_store_fold\\\]" }
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vFloat v = sfpi::dst_reg[0].mode<sfpi::DataLayout::F16b> ();
      sfpi::vFloat r = v;
      v_if (sfpi::abs(v) <= lam)
	{
	  r = 0.0f;
	}
      v_endif;
      sfpi::dst_reg[0].mode<sfpi::DataLayout::F16b> () = r;
      sfpi::dst_reg++;
    }
}
