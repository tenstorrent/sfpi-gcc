// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -ffinite-math-only -mtt-tensix-optimize-lut-select -mtt-tensix-optimize-lut-select-leaf-ext -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_lut_select" }
// laneHF refusing twin: the exact-obligation counting exempts only
// values that never compete for the eight-LREG file.  Here a genuinely
// held vector (an allocatable-LREG value read before the loop and
// stored after it) pins an LREG across the body on top of the six
// table slots and the row value, so the transactional budget still
// refuses (the kept-in-loop shape stays compilable at eight physical
// LREGs; a hoisted-coefficient shape would not be) and every coefficient materialization stays in the loop --
// the exemption is not a blanket lift of the pressure proof.
// { dg-final { scan-tree-dump-times "formed fp32-3entry-sgn-update \\(mod0 0\\) from 3-range magnitude dispatch tree, boundaries 0x3f800000,0x40000000, slot leaves mul0,affine,const" 1 "rvtt_lut_select" } }
// { dg-final { scan-tree-dump "coefficients kept in loop \\(lut-coefficient-pressure\\)" "rvtt_lut_select" } }
// { dg-final { scan-tree-dump-not "placed coefficient materialization" "rvtt_lut_select" } }
// { dg-final { scan-tree-dump "placements=0" "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }

extern volatile unsigned __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

__attribute__((noinline)) void
lut_tree_creg_addend_held_refuses ()
{
  sfpi::vFloat held = sfpi::l_reg[sfpi::LRegs::LReg7];
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
  sfpi::l_reg[sfpi::LRegs::LReg7] = held;
}
