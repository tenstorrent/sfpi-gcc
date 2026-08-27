// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-stochrnd-store-fold -fdump-tree-rvtt_store_fold" }
// WH arm of the licensed fire: the proof's semantics were lifted from
// the shared simulator arm both pinned oracles compile, and the
// WormholeB0 SFPSTORE.md/SFPSTOCHRND_FloatFloat.md functional models
// state the same truncation/ties-away divergence -- the license covers
// both proven targets (the pass gate already refuses elsewhere).
// { dg-final { scan-tree-dump-times "store-fold: licensed stochrnd fold" 1 "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "stochrnd-folded=1" "rvtt_store_fold" } }
// { dg-final { scan-assembler-not "SFPSTOCHRND" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
stochrndfold_fire_fp16b_wh ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vFloat a = sfpi::dst_reg[0];
      const sfpi::vFloat b = sfpi::dst_reg[32];
      sfpi::vFloat r = a - b;
      r = sfpi::convert<sfpi::vFloat16b>(r, sfpi::RoundMode::Nearest);
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
