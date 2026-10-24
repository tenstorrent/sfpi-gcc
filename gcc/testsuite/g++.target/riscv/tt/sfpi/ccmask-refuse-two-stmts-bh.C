// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// Check that a region with two predicated assignments is refused (ccmask-region-shape).
// { dg-final { scan-tree-dump "ccmask refused .ccmask-region-shape" "rvtt_ccmask" } }
// { dg-final { scan-assembler-not "SFPGT" } }
extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
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
