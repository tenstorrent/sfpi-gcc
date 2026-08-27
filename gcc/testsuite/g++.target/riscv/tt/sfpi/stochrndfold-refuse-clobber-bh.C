// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-stochrnd-store-fold -fdump-tree-rvtt_store_fold" }
// A CC region between the round and its store: the store's lane mask
// is no longer provably the round's, so the fold's masked-lanes
// argument is gone -- refuse even with the license token.  (Raw
// builtin form so the round feeds the store directly; mod1=1
// FP32_TO_FP16B, rnd=0 NEAREST.)
// { dg-final { scan-tree-dump "store-fold refused .stochrnd-store-fold-span-clobbered" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "stochrnd-folded=0" "rvtt_store_fold" } }
// { dg-final { scan-assembler "SFPSTOCHRND" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
stochrndfold_refuse_clobber ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vFloat a = sfpi::dst_reg[0];
      const sfpi::vFloat b = sfpi::dst_reg[32];
      sfpi::vFloat r = a - b;
      sfpi::vFloat q = __builtin_rvtt_sfpstochrnd_i (r.get (), 0, 1, 0);
      v_if (a < 0.0F)
	{
	  sfpi::dst_reg[16] = a;
	}
      v_endif;
      sfpi::dst_reg[0] = q;
      sfpi::dst_reg++;
    }
}
