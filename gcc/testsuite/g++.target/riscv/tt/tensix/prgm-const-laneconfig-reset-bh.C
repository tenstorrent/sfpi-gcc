// Check that undeclared LaneConfig default-reset SFPCONFIG words (0x910000F1) do not block the PRGM allocation.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "prgm-const: allocated PRGM L\\d+ for invariant immediate" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "raw SFPCONFIG writes LaneConfig" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 4 } }
// { dg-final { scan-assembler-not "SFPADDI" } }
// { dg-final { scan-assembler "SFPMAD" } }

void sfpu_lane_reset_init ()
{
  /* The _init_sfpu_config_reg idiom: three LaneConfig default-resets,
     no declaration markers.  */
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x910000F1));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x910000F1));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x910000F1));
}

void prgm_reset_fire ()
{
  // Both predicate flags and ROW_MASK are unknown at entry.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void defaulted_lane_state ()
{
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x910000F1));
}

void renamed_defaulted_gain ()
{
  // Both predicate flags and ROW_MASK are unknown at entry.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto west = __builtin_rvtt_sfpreadlreg (2);
  auto rate = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned step = 0; step != 24; ++step)
    {
      auto scaled = __builtin_rvtt_sfpmul (west, rate, 0);
      west = __builtin_rvtt_sfpaddi (nullptr, scaled, 0x4040, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (west, 2);
}
