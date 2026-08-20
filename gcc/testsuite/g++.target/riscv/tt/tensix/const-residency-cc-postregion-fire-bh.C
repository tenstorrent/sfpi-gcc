// cc-region granularity (laneDM widening): a CC-writing region the
// programming point can never be reached FROM does not defeat the
// all-lanes proof.  The reach-scoped proof admits the loop -- the
// post-loop CC block sits on no path to the entry-edge programming
// point, so the function-entry all-lanes state provably reaches it.
// The second function is the renamed, constant-varied twin.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "const-residency: allocated PRGM L1\\d for constant" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "cc-region-unproven" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }

void residency_postcc_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 16; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
    }
  /* A CC region after the loop: no CFG path leads from it back to the
     programming point.  */
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc_v (x, 0);
  __builtin_rvtt_sfppopc (0);
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_epilogue_masked (void)
{
  auto acc = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned step = 0; step != 24; ++step)
    {
      auto blend = __builtin_rvtt_sfpxloadi (nullptr, 0x3f2b850a, 0, 0, 31);
      acc = __builtin_rvtt_sfpmul (acc, blend, 0);
    }
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc_v (acc, 0);
  __builtin_rvtt_sfppopc (0);
  __builtin_rvtt_sfpwritelreg (acc, 2);
}
