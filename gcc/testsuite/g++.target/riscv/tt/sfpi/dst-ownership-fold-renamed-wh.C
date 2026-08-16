// Track B genericity: renamed-equivalent of the fold positive with
// different constants and body shape, on Wormhole.  The fold must key
// only on the typed operand tuple and effect facts, never on names,
// constants, or opcode words.
// { dg-options "-mcpu=tt-wh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-dst-ownership -fdump-rtl-rvtt_dst_ownership-details" }
// { dg-final { scan-rtl-dump-times "Dst-ownership fold: reload insn" 1 "rvtt_dst_ownership" } }
// { dg-final { scan-assembler-times {\mSFPLOAD\t} 1 } }

#include "dst-ownership-prologue.h"

using namespace sfpi;

__attribute__((noinline)) void
zug_quantize_row ()
{
  vFloat gamma = dst_reg[0];
  vFloat acc = gamma * -7.771561f + 0.0044717f;
  v_if (gamma >= 3.0F) { acc = 11.375f; }
  v_endif;
  acc = acc * gamma + acc;
  vFloat gamma_again = dst_reg[0];
  dst_reg[0] = copysgn (acc, gamma_again);
}
