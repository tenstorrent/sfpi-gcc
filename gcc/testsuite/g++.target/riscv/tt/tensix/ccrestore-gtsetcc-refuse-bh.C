// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -fdump-tree-rvtt_invariant" }
// R2 widening 1 near miss (control): an SFPGT SET_CC compare inside
// the balanced region is a CC modifier OUTSIDE the audited narrowing
// set -- the restore proof still holds (the POPC discards it), but the
// in-region candidate loses the containment fact and refuses BY NAME;
// the depth-0 candidate before the region still hoists.  The census
// channel names the unaudited modifier.
// { dg-final { scan-tree-dump "cc-position-widening-unproven" "rvtt_invariant" } }
// { dg-final { scan-tree-dump "in-region modifier sfpgt outside the audited narrowing set" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "Hoisted invariant SFPU immediate" 1 "rvtt_invariant" } }

void
ccr_gtsetcc_refuse ()
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
