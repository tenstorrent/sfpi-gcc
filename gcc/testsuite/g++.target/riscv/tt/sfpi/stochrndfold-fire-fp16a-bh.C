// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-stochrnd-store-fold -fdump-tree-rvtt_store_fold" }
// The licensed fold's FP16A row: a deterministic-nearest FP32_TO_FP16A
// round feeding the runtime-resolved (SRCB) converting store fires just
// like the FP16B row -- the licensed pair set is
// {FP16B->BF16, FP16A->FP16, either->SRCB} (the swept proof rows plus
// the SRCB-resolution precedent).
// { dg-final { scan-tree-dump-times "store-fold: licensed stochrnd fold" 1 "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "stochrnd-folded=1" "rvtt_store_fold" } }
// { dg-final { scan-assembler-not "SFPSTOCHRND" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
stochrndfold_fire_fp16a ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vFloat a = sfpi::dst_reg[0];
      const sfpi::vFloat b = sfpi::dst_reg[32];
      sfpi::vFloat r = a * b;
      r = sfpi::convert<sfpi::vFloat16a>(r, sfpi::RoundMode::Nearest);
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
