// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-stochrnd-store-fold -fdump-tree-rvtt_store_fold" }
// NO-LEAK control: the license token opens the pass gate but must NOT
// enable the value-preserving S1/S2 merge folds -- a plain predicated
// value merge (the threshold shape, no rounding anywhere) stays exactly
// as -mno-tt-tensix-optimize-store-fold would leave it: zero forwards,
// zero sinks, and the predicated merge word survives to the assembly.
// { dg-final { scan-tree-dump "store-fold: forwarded=0 sunk=0 sunk-licensed=0 stochrnd-folded=0" "rvtt_store_fold" } }
// { dg-final { scan-assembler "SFPMOV" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
stochrndfold_no_s1_leak (float t)
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vFloat v = sfpi::dst_reg[0];
      sfpi::vFloat r = v;
      v_if (v <= t)
	{
	  r = 0.0f;
	}
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
