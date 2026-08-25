// LOOP residency prefers unchanged code at equality and fires only for a
// strict delivered-word saving.  W2/R0 therefore refuses when two trips are
// not proven (including a possible one-trip execution) and fires at two;
// W2/R1 refuses at three and fires at four.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops --param max-completely-peeled-insns=0 -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "loop-profitability-unproven: strict-positive needs 2 proven trips; 2 materialization words, 0 resident read words, 3 programming words" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "loop-profitability-unproven: strict-positive needs 4 proven trips; 2 materialization words, 1 resident read words, 3 programming words" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "const-residency: allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 2 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }

void folded_one_trip_possible_refuses (unsigned n)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned i = 0; i != n; ++i)
    {
      auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void folded_positive_two_fires ()
{
  auto x = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned i = 0; i != 2; ++i)
    {
      auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x40e90fdb, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 1);
}

void standalone_equal_three_refuses ()
{
  for (unsigned i = 0; i != 3; ++i)
    {
      auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      __builtin_rvtt_sfpstore (nullptr, c, 0, 0, 0, 0, 7);
    }
}

void standalone_positive_four_fires ()
{
  for (unsigned i = 0; i != 4; ++i)
    {
      auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x40e90fdb, 0, 0, 31);
      __builtin_rvtt_sfpstore (nullptr, c, 0, 0, 0, 0, 7);
    }
}
