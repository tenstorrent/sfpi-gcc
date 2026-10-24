// Check that a reclaim window spanning unproven enclosing-loop content refuses the dead-claim reclaim.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-crossloop-hoist -mtt-tensix-optimize-loop-prgm-reclaim -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "loop-reclaim-call-window" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "reclaiming DEAD-claimed" "rvtt_prgm_const" } }

void owner_init (void)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (c0, 0, 12);
  auto k1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3dd8adac, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k1, 0, 13);
  auto k2 = __builtin_rvtt_sfpxloadi (nullptr, 0xbf317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k2, 0, 14);
}

void kernel_nested (void)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned oy = 0; oy != 4; ++oy)
    for (unsigned ix = 0; ix != 32; ++ix)
      {
	auto h1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f000000, 0, 0, 31);
	auto pi = __builtin_rvtt_sfpxloadi (nullptr, 0x40490fdb, 0, 0, 31);
	x = __builtin_rvtt_sfpmul (x, h1, 0);
	x = __builtin_rvtt_sfpadd (x, pi, 0);
      }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
