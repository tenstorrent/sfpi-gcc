// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc -fdump-tree-rvtt_reassoc" }
// Pressure-budget refusal (the corpus lreg-pressure finding: a
// pressure-blind licensed rebalance turned compilable Cos/Sin/I1/
// welford kernels into lreg-pressure-exceeded refusals): six extra
// vector values live across the chain leave no headroom for the
// balanced tree's additional simultaneously-live partial, so the chain
// refuses BY NAME even under the full license, and the kernel keeps
// compiling exactly as before.
// { dg-final { scan-tree-dump "reassoc-pressure-budget-exceeded" "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "licensed rebalance" "rvtt_reassoc" } }

void
ra_pressure_refuse (void)
{
  auto k0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto k1 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto k2 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto k3 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto k4 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto k5 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto x0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto x1 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto x2 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto x3 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto s1 = __builtin_rvtt_sfpadd (x0, x1, 0);
  auto s2 = __builtin_rvtt_sfpadd (s1, x2, 0);
  auto s3 = __builtin_rvtt_sfpadd (s2, x3, 0);
  /* The six k-values stay live past the chain.  */
  __builtin_rvtt_sfpstore (nullptr, s3, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfpstore (nullptr, k0, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfpstore (nullptr, k1, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfpstore (nullptr, k2, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfpstore (nullptr, k3, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfpstore (nullptr, k4, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfpstore (nullptr, k5, 0, 0, 0, 6, 7);
}
