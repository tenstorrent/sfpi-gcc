// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-stochrnd-store-fold -fdump-tree-rvtt_store_fold" }
// The store is vFloat-typed, so its Mod0 is SRCB: the delivered format
// is resolved at RUN time from ALU_ACC_CTRL_SFPU_Fp32_enabled /
// ALU_FORMAT_SPEC_REG*_SrcB, and one resolution (MOD0_FMT_FP32) is
// EXACT -- it converts nothing.  Folding there would delete the
// rounding outright instead of substituting the store's, so the
// license's own precondition (a format-converting store of the
// matching target precision) is not establishable at compile time.
// tt/proofs/stochrnd-store-round/ swept mod0=1 and mod0=2 only; there
// is no SRCB row, and the pass refuses by name with or without the
// license token.  Measured on Blackhole, the folded arm of the
// Float32/dest_acc=Yes fp16a cast was the identity on 4096/4096
// elements (craq-sfpi board/evidence/licensed-knob-boundary-20260930/).
// { dg-final { scan-tree-dump "store-fold refused .stochrnd-store-fold-format-mismatch" "rvtt_store_fold" } }
// { dg-final { scan-tree-dump "stochrnd-folded=0" "rvtt_store_fold" } }
// { dg-final { scan-assembler "SFPSTOCHRND" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
stochrndfold_refuse_srcb ()
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
