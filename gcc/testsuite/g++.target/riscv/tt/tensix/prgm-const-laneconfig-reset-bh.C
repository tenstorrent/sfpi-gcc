// The LaneConfig default-reset word class (SFPCONFIG dest 15, mod1
// bit0 = MOD1_IMM16_IS_VALUE set, imm16 = 0 -- the SFPU init's
// TTI_SFPCONFIG (0, 0xF, 1), word 0x910000F1) audits by class: the
// spec's VD == 15 arm writes LaneConfig only, so the programmable
// constant registers survive and the freedom proof passes with the
// reset words UNDECLARED.  The second function is the renamed,
// constant-varied twin over a single reset word.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "prgm-const: allocated PRGM L\\d+ for invariant immediate" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "raw SFPCONFIG writes LaneConfig" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }
// { dg-final { scan-assembler-not "SFPADDI" } }
// { dg-final { scan-assembler "SFPMAD" } }

void sfpu_lane_reset_init ()
{
  /* The real _init_sfpu_config_reg idiom: three LaneConfig
     default-resets, no declaration markers.  */
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x910000F1));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x910000F1));
  __asm__ __volatile__ (".ttinsn %0" : : "n" (0x910000F1));
}

void prgm_reset_fire ()
{
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
  auto west = __builtin_rvtt_sfpreadlreg (2);
  auto rate = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned step = 0; step != 24; ++step)
    {
      auto scaled = __builtin_rvtt_sfpmul (west, rate, 0);
      west = __builtin_rvtt_sfpaddi (nullptr, scaled, 0x4040, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (west, 2);
}
