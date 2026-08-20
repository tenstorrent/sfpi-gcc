// DAG list scheduling: the dual-Horner rational case.  Two independent
// polynomial accumulation chains (P and Q) written serially carry one
// modeled interlock stall per dependent adjacency (the audited mad-family
// one-slot result latency).  The list scheduler builds the region DAG and
// interleaves the chains: every latency shadow is filled by the other
// chain's next term, and the modeled makespan strictly decreases (the
// single-move bubble-fill phases cannot express this: each closes one
// slot with one instruction).  The second function is the renamed,
// opcode- and register-varied twin: the decision keys only on proven
// independence, audited latencies, and the modeled issue timeline.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-times "List-schedule: bb \\d+ nodes=6 makespan 11 -> 7 pressure-peak=\\d target=bh" 2 "rvtt_schedule" } }
// { dg-final { scan-assembler-not "SFPNOP" } }

void dual_horner_rational ()
{
  auto x  = __builtin_rvtt_sfpreadlreg (0);
  auto p  = __builtin_rvtt_sfpreadlreg (1);
  auto q  = __builtin_rvtt_sfpreadlreg (2);
  auto c1 = __builtin_rvtt_sfpreadlreg (3);
  auto c2 = __builtin_rvtt_sfpreadlreg (4);
  auto c3 = __builtin_rvtt_sfpreadlreg (5);
  p = __builtin_rvtt_sfpmad (p, x, c1, 0);
  p = __builtin_rvtt_sfpmad (p, x, c2, 0);
  p = __builtin_rvtt_sfpmad (p, x, c3, 0);
  q = __builtin_rvtt_sfpmad (q, x, c1, 0);
  q = __builtin_rvtt_sfpmad (q, x, c2, 0);
  q = __builtin_rvtt_sfpmad (q, x, c3, 0);
  __builtin_rvtt_sfpwritelreg (p, 1);
  __builtin_rvtt_sfpwritelreg (q, 2);
}

void renamed_pipeline_pq ()
{
  auto north = __builtin_rvtt_sfpreadlreg (6);
  auto south = __builtin_rvtt_sfpreadlreg (7);
  auto a1 = __builtin_rvtt_sfpmul (north, north, 0);
  auto a2 = __builtin_rvtt_sfpmul (a1, a1, 0);
  auto a3 = __builtin_rvtt_sfpmul (a2, a2, 0);
  auto b1 = __builtin_rvtt_sfpadd (south, south, 0);
  auto b2 = __builtin_rvtt_sfpadd (b1, b1, 0);
  auto b3 = __builtin_rvtt_sfpadd (b2, b2, 0);
  __builtin_rvtt_sfpwritelreg (a3, 6);
  __builtin_rvtt_sfpwritelreg (b3, 7);
}
