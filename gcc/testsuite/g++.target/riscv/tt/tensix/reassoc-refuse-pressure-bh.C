// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fassociative-math -fno-signed-zeros -fno-trapping-math -mtt-tensix-optimize-reassoc -fdump-tree-rvtt_reassoc" }
// Check that the rebalance refuses when live values leave no LREG for the extra partial (reassoc-pressure-budget-exceeded).
// { dg-final { scan-tree-dump "reassoc-pressure-budget-exceeded" "rvtt_reassoc" } }
// { dg-final { scan-tree-dump-not "licensed rebalance" "rvtt_reassoc" } }

void
ra_pressure_refuse (void)
{
  auto k0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto k1 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto k2 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto k3 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto x0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto x1 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto x2 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto x3 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
  auto s1 = __builtin_rvtt_sfpadd (x0, x1, 0);
  auto s2 = __builtin_rvtt_sfpadd (s1, x2, 0);
  auto s3 = __builtin_rvtt_sfpadd (s2, x3, 0);
  /* Four k-values plus the four inputs fit the original 8-LREG file.
     The proposed rebalance needs one additional partial and must refuse.
     Keeping six k-values would make the original program itself invalid,
     so that would not exercise the intended compilability contract.  */
  __builtin_rvtt_sfpstore (nullptr, s3, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfpstore (nullptr, k0, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfpstore (nullptr, k1, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfpstore (nullptr, k2, 0, 0, 0, 6, 7);
  __builtin_rvtt_sfpstore (nullptr, k3, 0, 0, 0, 6, 7);
}
