// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-stochrnd-store-fold -fdump-tree-rvtt_store_fold" }
// Cross-precision static pair: an FP32_TO_FP16B (7-bit) round feeding
// a statically FP16A-typed store (Mod0 FP16, 10-bit truncation) is
// OUTSIDE the swept proof rows -- the fold would change the delivered
// precision class, not just the rounding direction.  Refuse even with
// the license token.
// { dg-final { scan-tree-dump "store-fold refused .stochrnd-store-fold-format-mismatch" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "stochrnd-folded=0" "rvtt_store_fold" } }
// { dg-final { scan-assembler "SFPSTOCHRND" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
stochrndfold_refuse_format ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vFloat a = sfpi::dst_reg[0];
      const sfpi::vFloat b = sfpi::dst_reg[32];
      sfpi::vFloat r = a - b;
      sfpi::vFloat q = __builtin_rvtt_sfpstochrnd_i (r.get (), 0, 1, 0);
      sfpi::dst_reg[0] = sfpi::as<sfpi::vFloat16a> (q);
      sfpi::dst_reg++;
    }
}
