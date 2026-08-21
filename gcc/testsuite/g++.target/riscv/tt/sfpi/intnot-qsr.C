// { dg-options "-mcpu=tt-qsr32-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-int-not -fdump-tree-rvtt_int_not" }
// Target control: no QSR oracle is pinned, so the proof was not run
// there -- the pass fails closed by name and the subtract lowering is
// kept.
// { dg-final { scan-tree-dump "int-not refused .int-not-target-unproven" "rvtt_int_not" } }
// { dg-final { scan-assembler-not "SFPNOT" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
intnot_qsr ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      const sfpi::vInt v = sfpi::dst_reg[0];
      sfpi::dst_reg[0] = sfpi::vInt(-1) - v;
      sfpi::dst_reg++;
    }
}
