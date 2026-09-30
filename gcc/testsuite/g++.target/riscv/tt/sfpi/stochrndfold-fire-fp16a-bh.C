// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-stochrnd-store-fold -fdump-tree-rvtt_store_fold" }
// Even the licensed FP16A value pair cannot delete a PRNG advance that a
// caller may observe after this function returns.
// { dg-final { scan-tree-dump "stochrnd-store-fold-entropy-stream" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "stochrnd-folded=0" "rvtt_store_fold" } }
// { dg-final { scan-assembler "SFPSTOCHRND" } }
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
