// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc -fchecking=2 -fdump-tree-rvtt_reassoc" }
// Item-#10 verdict-identity twin: the reassoc pressure budget now
// queries the unified engine (rvtt_pressure_bb_peak, rvtt-pressure.cc)
// and under -fchecking recomputes the verdict through the verbatim
// legacy single-block counter and asserts equality at the query point.
// The refusal must fire by name exactly as it did when the counter
// lived in gimple-rvtt-reassoc.cc -- same shape as
// reassoc-refuse-pressure-bh.C, plus the checking leg.
// { dg-final { scan-tree-dump "reassoc-pressure-budget-exceeded" "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "licensed rebalance" "rvtt_reassoc" } }

void
ra_pressure_refuse_checked (void)
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
