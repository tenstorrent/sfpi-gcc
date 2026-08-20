// Fused-MAD admission (laneDM widening): the canonical SFPMAD form
// with materialized-constant operands.  Each in-loop invariant
// single-issue constant materialization feeding only the plain-mod
// sfpmad is parked in a PRGM register and the operand reads it back --
// value semantics are provable from the typed insn (per-lane A*B+C
// from operand values alone).  First function carries two constant
// operands (the sdpa exp-leg shape); the second is the renamed,
// constant-varied single-operand twin.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "prgm-const: allocated PRGM L1\\d for invariant immediate" 3 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 3 } }
// { dg-final { scan-assembler "SFPMAD" } }

void mad_fire ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto k = __builtin_rvtt_sfpxloadi (nullptr, 0x3fb8aa3b, 0, 0, 31);
      auto b = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
      x = __builtin_rvtt_sfpmad (x, k, b, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_damped_step ()
{
  auto vel = __builtin_rvtt_sfpreadlreg (2);
  auto pos = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned step = 0; step != 12; ++step)
    {
      auto damp = __builtin_rvtt_sfpxloadi (nullptr, 0x3f7ae148, 0, 0, 31);
      vel = __builtin_rvtt_sfpmad (vel, damp, pos, 0);
    }
  __builtin_rvtt_sfpwritelreg (vel, 2);
}
