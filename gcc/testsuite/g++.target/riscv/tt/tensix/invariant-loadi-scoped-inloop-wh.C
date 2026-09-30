// Opaque assembly inside the loop body is inside the hoist region: the
// loop must refuse byte-identically no matter what the assembly contains.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "function has opaque LREG state" 1 "rvtt_invariant" } }

void opaque_in_body ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      asm volatile (".ttinsn 0x74000000");
      auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000001, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
