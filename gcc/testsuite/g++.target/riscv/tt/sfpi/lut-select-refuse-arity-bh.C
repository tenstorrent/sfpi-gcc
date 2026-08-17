// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -fdump-tree-rvtt_lut_select" }
// Near miss: four range partitions exceed the FP32 3-entry capacity;
// refuse by arity, form nothing, change nothing.
// { dg-final { scan-tree-dump "refused \\(lut-partition-arity-unsupported\\)" "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-not "formed " "rvtt_lut_select" } }
// { dg-final { scan-assembler-not "SFPLUTFP32" } }

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
lut_tree_four_ranges ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat x = sfpi::dst_reg[0];
      sfpi::vFloat mag = sfpi::abs (x);
      sfpi::vFloat r = mag * 0.0913f + 0.4477f;
      v_if (mag < 1.0f) { r = mag * 0.1875f + 0.3125f; }
      v_elseif (mag < 2.0f) { r = mag * 0.2651f + -0.0442f; }
      v_elseif (mag < 4.0f) { r = mag * 0.1234f + 0.5678f; }
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
