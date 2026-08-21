// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-int-not -fdump-tree-rvtt_int_not" }
// The value-level one's complement (-1) - v folds to the single SFPNOT;
// the all-ones materialization and its LREG live range disappear.
// Proof artifact: tt/proofs/int-not-allones-subtract/ (EQUAL over 2^32).
// { dg-final { scan-tree-dump-times "int-not: folded all-ones subtract" 1 "rvtt_int_not" } }
// { dg-final { scan-assembler "SFPNOT\tL\[0-7\], L\[0-7\]" } }
// { dg-final { scan-assembler-not "SFPIADD" } }
// { dg-final { scan-assembler-not "SFPLOADI\t.*-1" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
intnot_fire ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vInt v = sfpi::dst_reg[0];
      sfpi::dst_reg[0] = sfpi::vInt(-1) - v;
      sfpi::dst_reg++;
    }
}
