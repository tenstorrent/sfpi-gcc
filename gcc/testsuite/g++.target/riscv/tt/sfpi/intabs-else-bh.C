// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-int-abs -fdump-tree-rvtt_int_abs" }
// The v_else spelling: `v_if (v >= 0) { } v_else { r = 0 - v; }` -- a
// single COMPC directly after the condition binds complements the
// CC_GE enabled set to exactly the proven {v < 0}, so the region
// reduces to the identical value function and folds to SFPABS.
// Reduction record: tt/proofs/int-abs-negate-select/REDUCTION.md.
// { dg-final { scan-tree-dump-times "int-abs: folded negate-select CC region" 1 "rvtt_int_abs" } }
// { dg-final { scan-assembler "SFPABS\tL\[0-7\], L\[0-7\], 0" } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPIADD" } }
// { dg-final { scan-assembler-not "SFPCOMPC" } }
// { dg-final { scan-assembler-not "SFPENCC" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
intabs_else_fire ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vInt v = sfpi::dst_reg[0];
      sfpi::vInt r = v;
      v_if (v >= 0) { } v_else { r = sfpi::vInt(0) - v; }
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
