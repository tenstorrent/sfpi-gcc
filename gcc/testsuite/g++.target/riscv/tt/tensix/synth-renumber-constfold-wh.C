// Wormhole twin of synth-renumber-constfold-bh.C (ICE reproducer):
// top-level constant adds on a synth_opcode value after complete
// peeling + constant propagation must renumber, not assert.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops" }
// { dg-final { scan-assembler-times {SFPLOAD} 4 } }
// { dg-final { scan-assembler {SFPLOAD\tL[0-9]+, 160, 4, 3} } }
// { dg-final { scan-assembler {SFPLOAD\tL[0-9]+, 162, 4, 3} } }
// { dg-final { scan-assembler {SFPLOAD\tL[0-9]+, 164, 4, 3} } }
// { dg-final { scan-assembler-not {:SFPLOAD} } }

void ice_fold_varaddr ()
{
  auto r = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 3);
  for (unsigned j = 0; j != 3; ++j)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 160 + 2 * j, 0, 0, 4, 3);
      r = __builtin_rvtt_sfpxor (r, v);
    }
  __builtin_rvtt_sfpstore (nullptr, r, 62, 0, 0, 4, 3);
}
