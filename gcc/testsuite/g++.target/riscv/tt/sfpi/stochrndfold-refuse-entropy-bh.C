// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-stochrnd-store-fold -fdump-tree-rvtt_store_fold" }
// PRNG-stream belt: the function contains a stochastic-rounding
// consumer, so deleting the deterministic candidate's hidden PRNG
// advance would shift the stream the consumer samples -- the
// otherwise-licensed candidate refuses.  (The stochastic site itself
// refuses mode-unlicensed.)
// { dg-final { scan-tree-dump "store-fold refused .stochrnd-store-fold-entropy-stream" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "store-fold refused .stochrnd-store-fold-mode-unlicensed" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "stochrnd-folded=0" "rvtt_store_fold" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
stochrndfold_refuse_entropy ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vFloat a = sfpi::dst_reg[0];
      const sfpi::vFloat b = sfpi::dst_reg[32];
      sfpi::vFloat r = a - b;
      r = sfpi::convert<sfpi::vFloat16b>(r, sfpi::RoundMode::Nearest);
      sfpi::dst_reg[0] = r;
      sfpi::vFloat s = a + b;
      s = sfpi::convert<sfpi::vFloat16b>(s, sfpi::RoundMode::NearestStochastic);
      sfpi::dst_reg[32] = s;
      sfpi::dst_reg++;
    }
}
