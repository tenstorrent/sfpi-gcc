// Renamed-equivalent, varied-constants twin of
// synth-renumber-constfold-bh.C: different base address, stride, load
// address and store row.  Proves the fix is shape-generic, not tied to
// the reproducer's constants.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }
// { dg-final { scan-assembler-times {SFPLOAD} 4 } }
// { dg-final { scan-assembler {SFPLOAD\tL[0-9]+, 40, 4, 7} } }
// { dg-final { scan-assembler {SFPLOAD\tL[0-9]+, 46, 4, 7} } }
// { dg-final { scan-assembler {SFPLOAD\tL[0-9]+, 52, 4, 7} } }
// { dg-final { scan-assembler-not {:SFPLOAD} } }

void fold_other_constants ()
{
  auto acc = __builtin_rvtt_sfpload (nullptr, 8, 0, 0, 4, 7);
  for (unsigned k = 0; k != 3; ++k)
    {
      auto q = __builtin_rvtt_sfpload (nullptr, 40 + 6 * k, 0, 0, 4, 7);
      acc = __builtin_rvtt_sfpxor (acc, q);
    }
  __builtin_rvtt_sfpstore (nullptr, acc, 30, 0, 0, 4, 7);
}
