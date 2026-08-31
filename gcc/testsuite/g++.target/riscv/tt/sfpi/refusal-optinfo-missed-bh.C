// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -fopt-info-missed -fdump-tree-rvtt_store_fold" }
// FABLE_GOES_BURR.md item #1: named refusals are routed through
// -fopt-info-missed (dual emission).  The S2 float-pair sink without
// the store-sink license token fires the standing named refusal
// store-fold-sink-format-canonicalizing through the registry: the
// legacy pass-dump line keeps its exact historical spelling, AND one
// structured 'missed' line reaches the -fopt-info destinations at the
// containing function's location.  The structured line must NOT leak
// into the pass dump stream (the twin suite pins those bytes).
// { dg-prune-output "missed: " }
// { dg-final { scan-tree-dump "store-fold refused .store-fold-sink-format-canonicalizing" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump-not "tt-refusal" "rvtt_store_fold" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void optinfo_float_shrink (float lam) // { dg-missed "tt-refusal: store-fold-sink-format-canonicalizing \\\[rvtt_store_fold\\\]" }
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vFloat v = sfpi::dst_reg[0];
      sfpi::vFloat r = v;
      v_if (sfpi::abs(v) <= lam)
	{
	  r = 0.0f;
	}
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
