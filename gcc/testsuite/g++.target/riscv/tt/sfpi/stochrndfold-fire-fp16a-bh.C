// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-stochrnd-store-fold -fdump-tree-rvtt_store_fold" }
// The licensed fold's FP16A row: a deterministic-nearest FP32_TO_FP16A
// round whose only consumer is a STATICALLY FP16A-typed converting Dst
// store (Mod0 FP16) fires just like the FP16B row.  The licensed pair
// set is exactly the two rows tt/proofs/stochrnd-store-round/ swept --
// {FP16A->FP16, FP16B->BF16}.  A vFloat-typed (Mod0 SRCB) store is not
// in it and refuses: stochrndfold-refuse-srcb-bh.C.
// { dg-final { scan-tree-dump-times "store-fold: licensed stochrnd fold" 1 "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "stochrnd-folded=1" "rvtt_store_fold" } }
// { dg-final { scan-assembler-not "SFPSTOCHRND" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
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
      sfpi::vFloat16a c
	= sfpi::convert<sfpi::vFloat16a>(r, sfpi::RoundMode::Nearest);
      sfpi::dst_reg[0] = c;
      sfpi::dst_reg++;
    }
}
