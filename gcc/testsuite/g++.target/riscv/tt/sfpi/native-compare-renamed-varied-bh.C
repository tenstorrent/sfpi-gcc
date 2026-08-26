// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-native-compare" }
// Renamed/varied twin: different symbol names, constants, and a
// vector-vector LE compare (the sfpxfcmpv path) -- the lowering keys
// only on the compare direction, never on names or the scalar path.
// { dg-final { scan-assembler-times "SFPLE\tL\[0-7\], L9, 0, 1" 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }
// { dg-final { scan-assembler-not "SFPCOMPC" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
qq_zebra_row ()
{
  for (int qk = 0; qk < 4; ++qk)
    {
      sfpi::vFloat lhs_val = sfpi::dst_reg[0];
      sfpi::vFloat rhs_val = sfpi::dst_reg[1];
      sfpi::vFloat acc = lhs_val * 0.125f + 3.0f;
      v_if (lhs_val <= rhs_val) { acc = acc + 7.0f; }
      v_endif;
      sfpi::dst_reg[0] = acc;
      sfpi::dst_reg++;
    }
}
