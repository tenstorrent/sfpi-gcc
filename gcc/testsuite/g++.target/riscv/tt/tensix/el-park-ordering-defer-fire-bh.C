// EL-vs-RESIDENCY ORDERING fire (candidates re-positioned by
// class): a CC-restore loop's IN-REGION invariant immediate (depth
// above zero -- the class whose early hoist forges lane merges and
// starves the late arbiter, the softplus-fresh anatomy) is
// EL-hoistable under the audited-narrowing admission, but with the
// late const-residency walk and its pressure-park tier enabled,
// -mtt-tensix-optimize-park-ordering defers the hoist to that walk --
// which allocates the same candidate under its exact pressure model
// and audited post-CC admission (here a programmable constant
// register).  Depth-zero candidates now stay with the early pass
// (park-seed-prefix-kept-bh.C); this twin pins the deferral on the
// in-region class.  The twin varies names, the constant, and the trip
// count.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-pressure-park -mtt-tensix-optimize-park-ordering -fdump-tree-rvtt_invariant-details -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "hoist deferred: residency-walk-ordering" 2 "rvtt_invariant" } }
// { dg-final { scan-tree-dump-not "Hoisted invariant SFPU immediate" "rvtt_invariant" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 2 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }

void ordering_defer_fire (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      __builtin_rvtt_sfppopc (0);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

void renamed_varied_walker (void)
{
  auto west = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned step = 0; step != 12; ++step)
    {
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (west, 0);
      auto bias = __builtin_rvtt_sfpxloadi (nullptr, 0x40e90fdb, 0, 0, 31);
      __builtin_rvtt_sfppopc (0);
      west = __builtin_rvtt_sfpmul (west, bias, 0);
    }
  __builtin_rvtt_sfpwritelreg (west, 2);
}
