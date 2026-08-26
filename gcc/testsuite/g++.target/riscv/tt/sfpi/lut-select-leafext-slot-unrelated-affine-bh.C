// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -ffinite-math-only -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-leaf-ext -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_lut_select" }
// laneHT unrelated-shape twin 2: a formation whose slot operands are
// all immediate materializations (affine leaves with literal
// coefficients, no constant-register read anywhere near a slot) is
// untouched by the slot conversion -- the LUT forms exactly as before
// and no FLOATB slot materialization line appears.
// { dg-final { scan-tree-dump "formed fp32-3entry-sgn-update" "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-not "materialized as FLOATB" "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-not "slot creg" "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
unrelated_all_affine_lut ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat x = sfpi::dst_reg[0];
      sfpi::vFloat a = sfpi::abs (x);
      sfpi::vFloat t = a * 0.25f + 0.75f;
      v_if (a < 1.0f) { t = a * 0.90625f + 0.0625f; }
      v_elseif (a < 2.0f) { t = a * 0.09375f + 0.8125f; }
      v_endif;
      sfpi::dst_reg[0] = t;
      sfpi::dst_reg++;
    }
}
