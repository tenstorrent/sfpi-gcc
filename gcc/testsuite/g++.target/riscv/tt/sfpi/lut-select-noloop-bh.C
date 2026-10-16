// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-lut-select -fdump-tree-rvtt_lut_select" }
// Check that a magnitude dispatch tree outside a loop, reading and writing LRegs, forms a LUT.
// { dg-final { scan-tree-dump-times "formed fp32-3entry-sgn-update" 1 "rvtt_lut_select" } }
// { dg-final { scan-assembler-times "SFPLUTFP32" 1 } }
// { dg-final { scan-assembler-not "SFPSETCC" } }

extern volatile unsigned long __instrn_buffer[];
namespace ckernel {
constexpr inline volatile unsigned long (&instrn_buffer)[] = ::__instrn_buffer;
}
#include <sfpi.h>

using namespace sfpi;

__attribute__((noinline)) void
straightline_activation ()
{
  vFloat v = l_reg[LRegs::LReg0];
  vFloat scale = l_reg[LRegs::LReg1];
  vFloat m = abs (v);
  vFloat out = m * 0.5111f + -0.9022f;
  v_if (m < 1.0f) { out = m * -0.25f + 0.75f; }
  v_elseif (m < 2.0f) { out = m * 1.5f + 0.0625f; }
  v_endif;
  l_reg[LRegs::LReg2] = out * scale;
}
