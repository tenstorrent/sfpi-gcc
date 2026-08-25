// CC-canonical peel profitability must price the resident read-back left in
// every unpeeled iteration.  All four loops have the same five-word body and
// three-word programming cost.  W2/R1 saves one word per remaining trip, so
// five trips refuse and six fire; the W2/R0 controls retain their existing
// three-trip refusal and four-trip fire boundary.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "refused .peel-trip-count-unproven" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "admits the CC-canonical peel" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "4 proven trips; 2 materialization words, 2 recurring-saving words" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "6 proven trips; 2 materialization words, 1 recurring-saving words" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "peeled first iteration" 2 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }

void readback_five_refuses ()
{
  for (unsigned i = 0; i != 5; ++i)
    {
      auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      __builtin_rvtt_sfpstore (nullptr, c, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (c, 0);
      __builtin_rvtt_sfppopc (0);
    }
}

void readback_six_fires ()
{
  for (unsigned i = 0; i != 6; ++i)
    {
      auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x40e90fdb, 0, 0, 31);
      __builtin_rvtt_sfpstore (nullptr, c, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (c, 0);
      __builtin_rvtt_sfppopc (0);
    }
}

void folded_three_refuses ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned i = 0; i != 3; ++i)
    {
      auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void folded_four_fires ()
{
  auto x = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned i = 0; i != 4; ++i)
    {
      auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x40e90fdb, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 1);
}
