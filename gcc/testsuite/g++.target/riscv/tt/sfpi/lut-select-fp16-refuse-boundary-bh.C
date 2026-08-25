// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-fp16 -fdump-tree-rvtt_lut_select" }
// A 6-range tree whose third boundary (1.25) is not an architectural
// LUT16 six-entry boundary embeds in no mode's boundary set: refuse
// by name, edit nothing.
// { dg-final { scan-tree-dump-times "refused \\(lut-boundary-mismatch\\)" 1 "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-not "formed" "rvtt_lut_select" } }
// { dg-final { scan-assembler-not "SFPLUTFP32" } }

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
lut_tree_fp16_bad_boundary ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat x = sfpi::dst_reg[0];
      sfpi::vFloat mag = sfpi::abs (x);
      sfpi::vFloat r = mag * 0.5f + 0.0f;
      v_if (mag < 0.5f) { r = mag * 0.1875f + 0.3125f; }
      v_elseif (mag < 1.0f) { r = mag * 0.265625f + 0.0859375f; }
      v_elseif (mag < 1.25f) { r = mag * 0.59375f + 0.404296875f; }
      v_elseif (mag < 2.0f) { r = mag * 0.609375f + 0.7421875f; }
      v_elseif (mag < 3.0f) { r = mag * 0.5390625f + 1.046875f; }
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
