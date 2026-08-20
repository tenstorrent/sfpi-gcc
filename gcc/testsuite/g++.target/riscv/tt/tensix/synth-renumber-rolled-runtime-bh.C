// Regression guard for the synth-renumber constant-fold fix: a rolled
// counted loop (trip count above the peeling limit) keeps its variable
// address and must still take the runtime-synthesized delivery path
// (instruction word built in a scalar register, one synth_opcode).
// This shape compiled identically before and after the fix.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }
// { dg-final { scan-assembler {sw\t[^\n]*# [0-9]+:SFPLOAD} } }
// { dg-final { scan-assembler-times {:SFPLOAD} 1 } }

void rolled_runtime ()
{
  auto acc = __builtin_rvtt_sfpload (nullptr, 8, 0, 0, 4, 7);
  for (unsigned k = 0; k != 5; ++k)
    {
      auto q = __builtin_rvtt_sfpload (nullptr, 40 + 6 * k, 0, 0, 4, 7);
      acc = __builtin_rvtt_sfpxor (acc, q);
    }
  __builtin_rvtt_sfpstore (nullptr, acc, 30, 0, 0, 4, 7);
}
