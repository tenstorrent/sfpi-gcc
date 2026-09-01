// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-cc-region-general -fdump-tree-rvtt_invariant" }
// R2 widening 1 fire: under the stage-B flag the SFPGT SET_CC compare
// joins the audited narrowing set (tt/proofs/cc-narrowing-writers/:
// the pinned simulator computes its per-lane flag decision inside the
// enable-masked for_each_lane, so a disabled lane stays disabled) --
// the in-region containment fact holds and BOTH invariant immediates
// hoist to the preheader.
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 2 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "cc-position-widening-unproven" "rvtt_invariant" } }

void
ccr_gtsetcc_fire ()
{
  for (int i = 0; i != 8; ++i)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, -32);
      auto y = __builtin_rvtt_sfpmad (x, c0, x, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      auto g = __builtin_rvtt_sfpgt (x, y, 1);
      auto ck = __builtin_rvtt_sfpxloadi (nullptr, 0x3f2e8ba3, 0, 0, -32);
      auto z = __builtin_rvtt_sfpmad (g, ck, y, 0);
      __builtin_rvtt_sfpstore (nullptr, z, 0, 0, 0, 6, 7);
      __builtin_rvtt_sfppopc (0);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
