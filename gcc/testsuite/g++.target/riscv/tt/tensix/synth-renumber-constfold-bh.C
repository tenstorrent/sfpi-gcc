// ICE reproducer: an sfpxor fold over
// variable-address sfploads in a counted loop.  Complete peeling plus
// constant propagation folds each iteration's synthesized-encoding
// operand to a constant, leaving adds of the form "sum = li + CST"
// directly on the synth_opcode value.  The renumber pass's graph
// builder assumed the level-0 add always keeps its variable operand
// and never marked such adds used -> gcc_assert (node.used) ICE at
// gimple-rvtt-synth.cc:553.  Post-fix the adds renumber 1:1 with
// addend-carrying synth_opcodes and the constant immediates reach the
// loads directly.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }
// { dg-final { scan-assembler-times {SFPLOAD} 4 } }
// { dg-final { scan-assembler {SFPLOAD\tL[0-9]+, 160, 4, 7} } }
// { dg-final { scan-assembler {SFPLOAD\tL[0-9]+, 162, 4, 7} } }
// { dg-final { scan-assembler {SFPLOAD\tL[0-9]+, 164, 4, 7} } }
// { dg-final { scan-assembler-not {:SFPLOAD} } }

void ice_fold_varaddr ()
{
  auto r = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
  for (unsigned j = 0; j != 3; ++j)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 160 + 2 * j, 0, 0, 4, 7);
      r = __builtin_rvtt_sfpxor (r, v);
    }
  __builtin_rvtt_sfpstore (nullptr, r, 62, 0, 0, 4, 7);
}
