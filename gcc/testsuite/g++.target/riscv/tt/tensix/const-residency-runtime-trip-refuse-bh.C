// LOOP residency must prove its delivery break-even.  Runtime trip counts do
// not prove even the two trips needed when a two-word materialization folds
// directly into an arithmetic consumer.  Renamed/signed-bound twin included.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "loop-profitability-unproven: break-even needs 2 proven trips; 2 materialization words, 0 resident read words, 3 programming words" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "const-residency: allocated PRGM" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

void residency_runtime_trips_refuses (unsigned n)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != n; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_scaled_rows_refuses (int rows)
{
  auto acc = __builtin_rvtt_sfpreadlreg (4);
  for (int r = 0; r < rows; ++r)
    {
      auto w = __builtin_rvtt_sfpxloadi (nullptr, 0x3f4ccccd, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, w, 0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 4);
}
