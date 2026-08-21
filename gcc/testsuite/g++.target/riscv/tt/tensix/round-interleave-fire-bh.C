// Round-chain interleave FIRE: a counted round loop whose iterations
// are independent by dataflow -- the body is a two-op latency chain
// from loop-invariant inputs plus a single-statement reduction tail
// (the only loop-carried value, one word on its recurrence circuit).
// The gimple pass proves the obligations and requests unroll-by-two;
// the RTL unroller doubles the body; the cyclic list-scheduler
// extension lifts the self-loop deferral and interleaves the two
// copies' chains, committing on a strict steady-state II decrease.
// The reorder never crosses the backedge and every dependence edge is
// honored, so per-iteration semantics (and the reduction order) are
// bit-exact by construction.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-round-interleave -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_round_interleave -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-tree-dump "round-interleave: requested unroll 2 of loop" "rvtt_round_interleave" } }
// { dg-final { scan-tree-dump "round-interleave: pressure bound \\d\\+\\d within the 8-LREG file" "rvtt_round_interleave" } }
// { dg-final { scan-rtl-dump "List-schedule \\(round-interleave cyclic\\): bb \\d+ nodes=6 II \\d+ -> \\d+ renames=\\d+ target=bh" "rvtt_schedule" } }

void rci_rounds ()
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
