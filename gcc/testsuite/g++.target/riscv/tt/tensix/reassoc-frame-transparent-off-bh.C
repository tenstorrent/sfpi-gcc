// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc -fdump-tree-rvtt_reassoc" }
// Check that without -mtt-tensix-optimize-cc-region-general the nested CC frame still refuses the rebalance (reassoc-cc-region-boundary).
// { dg-final { scan-tree-dump-times "reassoc-cc-region-boundary" 1 "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "licensed rebalance" "rvtt_reassoc" } }

void
ra_frame_off (void)
{
  auto x0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto x1 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto x2 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto x3 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc (x0, 0);
  auto s1 = __builtin_rvtt_sfpadd (x0, x1, 0);
  auto s2 = __builtin_rvtt_sfpadd (s1, x2, 0);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc (x1, 0);
  auto zz = __builtin_rvtt_sfpassign_lv (x2, x1);
  __builtin_rvtt_sfpstore (nullptr, zz, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfppopc (0);
  auto s3 = __builtin_rvtt_sfpadd (s2, x3, 0);
  __builtin_rvtt_sfpstore (nullptr, s3, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfppopc (0);
}
