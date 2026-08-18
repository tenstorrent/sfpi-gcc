// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// Near miss: two predicated assignments -- not a single zeroing; the
// AND identity covers exactly one zero merge.  Refuse by name.
// { dg-final { scan-tree-dump "ccmask refused .ccmask-region-shape" "rvtt_ccmask" } }
// { dg-final { scan-assembler-not "SFPGT" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
ccmask_two_stmts ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat x = sfpi::dst_reg[0];
      sfpi::vFloat w = x * 0.5f;
      sfpi::vFloat u = x + 2.0f;
      v_if (x <= 0.0f) { w = 0.0f; u = 0.0f; }
      v_endif;
      sfpi::dst_reg[0] = w + u;
      sfpi::dst_reg++;
    }
}
