// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -ffinite-math-only -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-leaf-ext -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_lut_select" }
// Unrelated-shape twin 1: no LUT forms here, so the slot
// conversion has nothing to touch -- a plain row loop whose mad reads
// the hardwired 1.0 keeps its direct creg addend (creg-capable
// operand position; no physical slot is involved) and no FLOATB slot
// materialization appears.
// { dg-final { scan-tree-dump-not "formed fp32" "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-not "materialized as FLOATB" "rvtt_lut_select" } }
// { dg-final { scan-assembler-not "SFPLUTFP32" } }
// { dg-final { scan-assembler "SFPMAD\tL\[0-9\]+, L\[0-9\]+, L\[0-9\]+, L10" } }

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
unrelated_plain_mad_row ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat x = sfpi::dst_reg[0];
      sfpi::dst_reg[0] = x * (-x) + 1.0f;
      sfpi::dst_reg++;
    }
}
