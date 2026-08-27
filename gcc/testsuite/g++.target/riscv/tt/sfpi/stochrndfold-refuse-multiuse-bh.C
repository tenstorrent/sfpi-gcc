// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-stochrnd-store-fold -fdump-tree-rvtt_store_fold" }
// The rounded value is consumed BEYOND the store (an accumulator):
// deleting the rounding instruction would change the OTHER consumer's
// value class entirely, not just the stored bits -- refuse even with
// the license token.  (Raw builtin form: the typed convert wrapper's
// merge would gate on its own multi-use first; the builtin is the
// wrapper's own lowering, mod1=1 FP32_TO_FP16B, rnd=0 NEAREST.)
// { dg-final { scan-tree-dump "store-fold refused .stochrnd-store-fold-multi-use" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "stochrnd-folded=0" "rvtt_store_fold" } }
// { dg-final { scan-assembler "SFPSTOCHRND" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
stochrndfold_refuse_multiuse ()
{
  sfpi::vFloat acc = 0.0f;
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vFloat a = sfpi::dst_reg[0];
      const sfpi::vFloat b = sfpi::dst_reg[32];
      sfpi::vFloat r = a - b;
      sfpi::vFloat q = __builtin_rvtt_sfpstochrnd_i (r.get (), 0, 1, 0);
      sfpi::dst_reg[0] = q;
      acc += q;
      sfpi::dst_reg++;
    }
  sfpi::dst_reg[64] = acc;
}
