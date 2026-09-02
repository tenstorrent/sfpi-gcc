// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-stochrnd-store-fold -fdump-tree-rvtt_store_fold" }
// The LICENSED stochrnd-into-store fold on the binary-float
// shape: the -mtt-tensix-optimize-stochrnd-store-fold license token
// ALONE (it gates the pass by itself; the S1/S2 merge folds stay off) --
// the explicit deterministic-nearest FP32_TO_FP16B rounding word whose
// only consumer is the converting Dst store is deleted through the
// convert wrapper's all-lanes merge (validated against the S1 same-mask
// contract) -- the store's own conversion path delivers the handwritten
// idiom's truncating bits (the value change the license admits:
// tt/proofs/stochrnd-store-round NOT-EQUAL, BF16 row 2,155,741,184/2^32;
// classes round-up / -0 / denormal-sign / NaN->Inf).  The row body drops
// from 5 to 4 delivered words.
// { dg-final { scan-tree-dump-times "store-fold: licensed stochrnd fold" 1 "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "stochrnd-folded=1" "rvtt_store_fold" } }
// { dg-final { scan-assembler-not "SFPSTOCHRND" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
stochrndfold_fire_fp16b ()
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
