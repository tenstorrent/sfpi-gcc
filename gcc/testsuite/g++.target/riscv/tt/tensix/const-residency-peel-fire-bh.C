// Residency fire, CC-canonical peel class: the loop body
// carries a lowered v_if region ending in the all-lanes SFPENCC (the
// fresh-body kernel shape), so the plain LOOP class's sfpu-barrier and
// the whole-function cc-region-unproven refusals do not apply.  The
// first iteration is peeled statement for statement onto the entry
// edge -- reproducing its behavior under ANY ambient lane state -- and
// the staging SFPLOADI + SFPCONFIG programming lands after the peeled
// copy's own all-lanes SFPENCC, where every lane is provably enabled
// (the architectural requirement on SFPCONFIG).  Iterations 2..N read
// the constant register instead of re-materializing the paired
// SFPLOADI.  The twin varies names, the constant, and the trip count.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "admits the CC-canonical peel" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "peeled first iteration" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 2 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }
// The loop bodies read the programmed register; the only remaining
// SFPLOADI pairs are the peeled first iterations and the staging
// loads (two pairs per function).
// { dg-final { scan-assembler-times "SFPLOADI" 8 } }

void peel_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_varied_scale (void)
{
  auto west = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned step = 0; step != 12; ++step)
    {
      auto bias = __builtin_rvtt_sfpxloadi (nullptr, 0x40e90fdb, 0, 0, 31);
      west = __builtin_rvtt_sfpmul (west, bias, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (west, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (west, 2);
}
