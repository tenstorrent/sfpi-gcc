// Isomorphic-pair lift: EXACTLY TWO regions with one insn-code
// signature schedule under ONE shared permutation, so the copies stay
// textually isomorphic for the replay/MOP re-roll.  fn 1: the pair
// fires -- each copy's serial P-chain stall is filled by its own
// independent Q term, both copies commit the identical order (each
// judged by its own boundary model, transactional across the pair).
// fn 2: same insn-code signature but DIFFERENT dataflow (copy 1 is
// three independent mads, copy 2 chains its first two): the positional
// dependence-matrix proof fails and the pair refuses by name, keeping
// the original deferral semantics.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-round-interleave -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-times "List-schedule: bb \\d+ nodes=3 makespan \\d+ -> \\d+ target=bh" 2 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "List-schedule \\(round-interleave iso-pair\\): bb \\d+ regions at uid=\\d+/uid=\\d+ share one permutation" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "List-schedule refused: copies-not-dataflow-isomorphic at uid=\\d+/uid=\\d+" "rvtt_schedule" } }

void iso_pair_fires ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto p = __builtin_rvtt_sfpreadlreg (1);
  auto q = __builtin_rvtt_sfpreadlreg (2);
  auto b = __builtin_rvtt_sfpreadlreg (3);
  // Copy 1: serial p-chain (one modeled stall) + independent q term.
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc_v (b, 0);
  __builtin_rvtt_sfppopc (0);
  // Copy 2: the same region shape and the same dataflow.
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  __builtin_rvtt_sfpwritelreg (p, 1);
  __builtin_rvtt_sfpwritelreg (q, 2);
}

void iso_bait_refuses ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto p = __builtin_rvtt_sfpreadlreg (1);
  auto q = __builtin_rvtt_sfpreadlreg (2);
  auto r = __builtin_rvtt_sfpreadlreg (4);
  auto b = __builtin_rvtt_sfpreadlreg (3);
  // Copy 1: three INDEPENDENT mads.
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  r = __builtin_rvtt_sfpmad (r, x, r, 0);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc_v (b, 0);
  __builtin_rvtt_sfppopc (0);
  // Copy 2: same insn codes, different dataflow (p-chain first).
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  q = __builtin_rvtt_sfpmad (q, x, q, 0);
  __builtin_rvtt_sfpwritelreg (p, 1);
  __builtin_rvtt_sfpwritelreg (q, 2);
  __builtin_rvtt_sfpwritelreg (r, 4);
}
