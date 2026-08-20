// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-int-abs -fdump-tree-rvtt_int_abs" }
// The non-strict polarity: `v_if (v <= 0) { r = 0 - v; }` -- the
// enabled set gains exactly raw v == 0 (unique in two's complement),
// where the wrapping negation is the identity 0 - 0 = 0, so the value
// function is pointwise the proven one; folds to SFPABS.  Reduction
// record: tt/proofs/int-abs-negate-select/REDUCTION.md.
// { dg-final { scan-tree-dump-times "int-abs: folded negate-select CC region" 1 "rvtt_int_abs" } }
// { dg-final { scan-assembler "SFPABS\tL\[0-7\], L\[0-7\], 0" } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPIADD" } }
// { dg-final { scan-assembler-not "SFPENCC" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
intabs_lepolarity_fire ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vInt v = sfpi::dst_reg[0];
      sfpi::vInt r = v;
      v_if (v <= 0) { r = sfpi::vInt(0) - v; }
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
