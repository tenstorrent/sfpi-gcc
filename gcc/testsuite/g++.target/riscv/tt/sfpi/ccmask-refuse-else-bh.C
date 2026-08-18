// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-ccmask -fdump-tree-rvtt_ccmask" }
// Near miss: an else arm assigns on the complementary lanes -- both
// sides write, so the single AND merge does not apply.  Refuse by name.
// { dg-final { scan-tree-dump "ccmask refused .ccmask-region-shape" "rvtt_ccmask" } }
// { dg-final { scan-assembler-not "SFPGT" } }
extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>
__attribute__((noinline)) void
ccmask_else ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat x = sfpi::dst_reg[0];
      sfpi::vFloat w = x * 0.5f;
      v_if (x <= 0.0f) { w = 0.0f; }
      v_else { w = w + 1.0f; }
      v_endif;
      sfpi::dst_reg[0] = w;
      sfpi::dst_reg++;
    }
  }
