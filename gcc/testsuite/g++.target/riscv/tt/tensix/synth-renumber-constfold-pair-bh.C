// Adjacent-shape twin of synth-renumber-constfold-bh.C: only TWO
// peeled iterations, i.e. two top-level constant adds sharing one
// synth_opcode.  Also ICEd at gimple-rvtt-synth.cc:553 pre-fix.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }
// { dg-final { scan-assembler-times {SFPLOAD} 3 } }
// { dg-final { scan-assembler {SFPLOAD\tL[0-9]+, 160, 4, 7} } }
// { dg-final { scan-assembler {SFPLOAD\tL[0-9]+, 162, 4, 7} } }
// { dg-final { scan-assembler-not {:SFPLOAD} } }

void fold_two ()
{
  auto acc = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
  for (unsigned j = 0; j != 2; ++j)
    {
      auto q = __builtin_rvtt_sfpload (nullptr, 160 + 2 * j, 0, 0, 4, 7);
      acc = __builtin_rvtt_sfpxor (acc, q);
    }
  __builtin_rvtt_sfpstore (nullptr, acc, 62, 0, 0, 4, 7);
}
