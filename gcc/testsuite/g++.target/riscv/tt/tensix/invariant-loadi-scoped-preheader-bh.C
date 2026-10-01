// An opaque block terminator in the dedicated preheader executes after any
// end-of-block insertion point, so it is inside the hoist region and the
// loop must refuse.  Opacity earlier in the preheader is outside the region.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "function has opaque LREG state" 1 "rvtt_invariant" } }

void opaque_preheader_tail ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  asm goto (".ttinsn 0x75000000" :::: entry);
 entry:
  for (unsigned ix = 0; ix != 8; ++ix)
    {
      auto c = __builtin_rvtt_sfpxloadi (nullptr, 0x3e000001, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, c, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
