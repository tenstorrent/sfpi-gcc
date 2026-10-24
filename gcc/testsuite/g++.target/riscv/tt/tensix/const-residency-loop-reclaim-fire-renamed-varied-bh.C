// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-loop-prgm-reclaim -fdump-tree-rvtt_prgm_const-details" }
// Check that the loop PRGM reclaim gives the same results with renamed variables, other constants and trip count.
// { dg-final { scan-tree-dump-times "reusing TU-programmed PRGM L12 .every TU write stores 0x3ca3d70a" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "reclaiming DEAD-claimed PRGM L13 for 0x3f2e147b .loop class" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "reclaiming DEAD-claimed PRGM L14 for 0x419d70a4 .loop class" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "refused .prgm-exhausted." 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L1\\d for constant 0x\[0-9a-f\]+ .loop class" 4 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 9 } }

void audit_ip_claimer (void)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto s0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3ca3d70a, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (s0, 0, 12);
  auto s1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3d4ccccd, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (s1, 0, 13);
  auto s2 = __builtin_rvtt_sfpxloadi (nullptr, 0xbe99999a, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (s2, 0, 14);
}

void audit_ip_row_body (void)
{
  // Prove physical full lanes; do not infer an entry-state ABI.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto z = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned lap = 0; lap != 21; ++lap)
    {
      auto a1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3ca3d70a, 0, 0, 31);
      auto a2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3ca3d70a, 0, 0, 31);
      auto b1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f2e147b, 0, 0, 31);
      auto b2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f2e147b, 0, 0, 31);
      auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x419d70a4, 0, 0, 31);
      auto d1 = __builtin_rvtt_sfpxloadi (nullptr, 0x42b3a5e3, 0, 0, 31);
      z = __builtin_rvtt_sfpmul (z, a1, 0);
      z = __builtin_rvtt_sfpadd (z, a2, 0);
      z = __builtin_rvtt_sfpmul (z, b1, 0);
      z = __builtin_rvtt_sfpadd (z, b2, 0);
      z = __builtin_rvtt_sfpmul (z, c1, 0);
      z = __builtin_rvtt_sfpadd (z, d1, 0);
    }
  __builtin_rvtt_sfpwritelreg (z, 1);
}
