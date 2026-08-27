// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -mtt-tensix-optimize-stochrnd-store-fold -fdump-tree-rvtt_store_fold" }
// Composition control: with BOTH -mtt-tensix-optimize-store-fold and
// the license token, the S1 merge forward runs first and the fold sees
// the exposed direct shape -- one forward, one licensed fold, the same
// final words as the license-only leg.
// { dg-final { scan-tree-dump-times "store-fold: licensed stochrnd fold" 1 "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "store-fold: forwarded=1 sunk=0 sunk-licensed=0 stochrnd-folded=1" "rvtt_store_fold" } }
// { dg-final { scan-assembler-not "SFPSTOCHRND" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
stochrndfold_fire_composed ()
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
