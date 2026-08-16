// Varied-constant, varied-trip variant of the scoped-outside shape: the
// decisions must key on structure, never on particular values or counts.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 3 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "function has opaque LREG state" "rvtt_invariant" } }

void varied_nested_shared_entry ()
{
  asm volatile (".ttinsn 0x7a000000");
  auto x = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned face = 0; face != 3; ++face)
    {
      for (unsigned ix = 0; ix != 5; ++ix)
	{
	  auto a = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
	  auto b = __builtin_rvtt_sfpxloadi (nullptr, 0x00003a29, 0, 0, 31);
	  auto c = __builtin_rvtt_sfpxloadi (nullptr, 0xc2fe0000, 0, 0, 31);
	  x = __builtin_rvtt_sfpmad (x, a, b, 0);
	  x = __builtin_rvtt_sfpmul (x, c, 0);
	}
      asm volatile (".ttinsn 0x7b000000");
    }
  __builtin_rvtt_sfpwritelreg (x, 1);
  asm volatile (".ttinsn 0x7c000000");
}
