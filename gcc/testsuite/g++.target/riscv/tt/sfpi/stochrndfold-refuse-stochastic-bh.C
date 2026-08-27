// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-stochrnd-store-fold -fdump-tree-rvtt_store_fold" }
// NEAR-MISS (rounding modes differ): the same shape with STOCHASTIC
// rounding must refuse even WITH the license token -- stochastic
// rounding is a semantic entropy feature (the PRNG sample decides the
// round) and the proof's swept rows are the deterministic-nearest
// mode only.  The explicit rounding word stays.
// { dg-final { scan-tree-dump "store-fold refused .stochrnd-store-fold-mode-unlicensed" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "stochrnd-folded=0" "rvtt_store_fold" } }
// { dg-final { scan-assembler "SFPSTOCHRND" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
stochrndfold_refuse_stochastic ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vFloat a = sfpi::dst_reg[0];
      const sfpi::vFloat b = sfpi::dst_reg[32];
      sfpi::vFloat r = a - b;
      r = sfpi::convert<sfpi::vFloat16b>(r, sfpi::RoundMode::NearestStochastic);
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
