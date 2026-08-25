// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-fp16 -fdump-tree-rvtt_lut_select" }
// The packed LUT16 modes need every coefficient's compile-time value:
// a run-time coefficient (loaded from Dst) refuses by name and edits
// nothing.
// { dg-final { scan-tree-dump-times "refused \\(lut-coeff-value-unproven\\)" 1 "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-not "formed" "rvtt_lut_select" } }
// { dg-final { scan-assembler-not "SFPLUTFP32" } }

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
lut_tree_fp16_runtime_coeff ()
{
  sfpi::vFloat rt = sfpi::dst_reg[8];
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat x = sfpi::dst_reg[0];
      sfpi::vFloat mag = sfpi::abs (x);
      sfpi::vFloat r = mag * 0.5f + 0.0f;
      v_if (mag < 0.5f) { r = mag * rt + 0.3125f; }
      v_elseif (mag < 1.0f) { r = mag * 0.265625f + 0.0859375f; }
      v_elseif (mag < 1.5f) { r = mag * 0.59375f + 0.404296875f; }
      v_elseif (mag < 2.0f) { r = mag * 0.609375f + 0.7421875f; }
      v_elseif (mag < 3.0f) { r = mag * 0.5390625f + 1.046875f; }
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
