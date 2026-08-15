// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }
// { dg-final { scan-assembler-times "SFPLOADI" 2 } }

static volatile unsigned impostor_buffer[1];

void invariant_loadi_impostor ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      auto a = __builtin_rvtt_sfpxloadi (&impostor_buffer[0], 0x3e4b1a3d,
					0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, a, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
