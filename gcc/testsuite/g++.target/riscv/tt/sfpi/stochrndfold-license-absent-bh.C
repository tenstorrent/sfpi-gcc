// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-store-fold -fdump-tree-rvtt_store_fold" }
// THE critical refusal: the exact licensed-fire shape (binary-float
// class: deterministic-nearest FP32_TO_FP16B round whose only consumer
// is the converting store) WITHOUT the
// -mtt-tensix-optimize-stochrnd-store-fold license token keeps the
// standing named refusal byte-identically -- nothing value-changing
// fires without the owner's token.  The explicit rounding word stays.
// { dg-final { scan-tree-dump "store-fold refused .stochrnd-store-rounding-divergent" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "stochrnd-folded=0" "rvtt_store_fold" } }
// { dg-final { scan-assembler "SFPSTOCHRND" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
stochrndfold_license_absent ()
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
