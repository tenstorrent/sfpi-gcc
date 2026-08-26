// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -ffinite-math-only -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-leaf-ext -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_lut_select" }
// laneHF (tanhderivlut +47.5% silicon residual): the loop's `+ 1.0f'
// addend reads the hardwired constant register (LReg[10]); the earlier
// invariant hoist places that read in the preheader.  The historical
// FP32-direct LREG budget counted the hoisted creg read as a ninth
// pinned LREG and transactionally forfeited the WHOLE coefficient
// placement -- every table word rematerialized per row (the 8-word
// loop behind the +47.5%).  Under the leaf extension the budget now
// uses the exact-obligation counting (a creg read whose uses are all
// creg-capable positions never competes for the eight-LREG file), so
// every coefficient ends in the preheader and the loop keeps the
// load/LUT/mad/store shape.
// { dg-final { scan-tree-dump-times "formed fp32-3entry-sgn-update \\(mod0 0\\) from 3-range magnitude dispatch tree, boundaries 0x3f800000,0x40000000, slot leaves mul0,affine,const" 1 "rvtt_lut_select" } }
// The earlier invariant hoist has already placed the three value
// immediates and the addend read in the preheader; the two zero-slot
// materializations formation creates at the LUT site are what remain
// to place (exactly the real tanhderivlut TU shape).
// { dg-final { scan-tree-dump-times "placed coefficient materialization in loop preheader" 2 "rvtt_lut_select" } }
// { dg-final { scan-tree-dump "placements=2" "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-not "lut-coefficient-pressure" "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
lut_tree_creg_addend_places ()
{
  for (int ix = 0; ix < 8; ++ix)
    {
      sfpi::vFloat x = sfpi::dst_reg[0];
      sfpi::vFloat a = sfpi::abs (x);
      sfpi::vFloat t = 1.0f;
      v_if (a < 1.0f) { t = a * 0.90625f; }
      v_elseif (a < 2.0f) { t = a * 0.09375f + 0.8125f; }
      v_endif;
      sfpi::dst_reg[0] = t * (-t) + 1.0f;
      sfpi::dst_reg++;
    }
}
