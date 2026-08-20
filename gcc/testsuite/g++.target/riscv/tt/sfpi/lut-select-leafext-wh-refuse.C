// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-leaf-ext -fdump-tree-rvtt_lut_select" }
// Wormhole: no extension leaf class is certified without a
// finite-math license -- the tree-vs-slot bucket agreement itself
// fails on WH for negative-NaN inputs (SFPABS keeps the -NaN sign and
// the WH compare-subtract inherits it, routing those lanes to range 0
// while the LUT buckets them by magnitude into the tail; WH NaN
// results also carry non-canonical payloads).  Refuse by name: the
// whole-formation WH guard (lut-wh-negative-nan-divergent) fires at
// the capability check, ahead of the per-slot leaf admission.
// { dg-final { scan-tree-dump "refused \\(lut-wh-negative-nan-divergent\\)" "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-not "formed " "rvtt_lut_select" } }
// { dg-final { scan-assembler-not "SFPLUTFP32" } }

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
lut_tree_mul0_wh ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat x = sfpi::dst_reg[0];
      sfpi::vFloat mag = sfpi::abs (x);
      sfpi::vFloat r = mag * 0.0913f + 0.4477f;
      v_if (mag < 1.0f) { r = mag * 0.1875f; }
      v_elseif (mag < 2.0f) { r = mag * 0.2651f + -0.0442f; }
      v_endif;
      sfpi::dst_reg[0] = r;
      sfpi::dst_reg++;
    }
}
