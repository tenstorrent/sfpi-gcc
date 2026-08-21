// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-round-interleave -fdump-tree-rvtt_round_interleave" }
// QSR has no audited latency or replay/seam model for this request.
// { dg-final { scan-tree-dump "refused .round-interleave-qsr-unproven." "rvtt_round_interleave" } }

void qsr_rounds ()
{
  auto x   = __builtin_rvtt_sfpreadlreg (0);
  auto acc = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned r = 0; r != 32; ++r)
    {
      auto t1 = __builtin_rvtt_sfpmul (x, x, 0);
      auto t2 = __builtin_rvtt_sfpmad (t1, x, x, 0);
      acc = __builtin_rvtt_sfpand (acc, t2);
    }
  __builtin_rvtt_sfpwritelreg (acc, 2);
}
