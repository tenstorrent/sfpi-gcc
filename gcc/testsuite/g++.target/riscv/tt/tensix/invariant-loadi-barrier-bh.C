// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }

volatile unsigned ordinary_memory;

void memory_barrier ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      ordinary_memory = ix;
      auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void cc_barrier ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      __builtin_rvtt_sfpencc (3, 10);
      auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void config_barrier ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      __builtin_rvtt_sfpwriteconfig_v (x, 0);
      auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void gpr_barrier ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      asm volatile ("" ::: "a0");
      auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
