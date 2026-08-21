// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant-details" }
// SFPENCC at push depth zero rewrites the ambient lane-enable state
// with no enclosing save: when the loop runs under a narrowed caller
// mask, iteration 2's load executes in the ENCC-widened state -- wider
// than the preheader's -- so a naive hoist would corrupt the widened
// lanes.  Refuse the loop by name.
// { dg-final { scan-tree-dump "cc-restore-ambient-cc-write" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }

void ambient_encc ()
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
