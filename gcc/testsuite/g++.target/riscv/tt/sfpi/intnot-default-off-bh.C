// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti" }
// Default off: without -mtt-tensix-optimize-int-not the subtract form
// keeps its two-word lowering byte-identically.
// { dg-final { scan-assembler-not "SFPNOT" } }
// { dg-final { scan-assembler "SFPIADD" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
intnot_default_off ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vInt v = sfpi::dst_reg[0];
      sfpi::dst_reg[0] = sfpi::vInt(-1) - v;
      sfpi::dst_reg++;
    }
}
