// CC-region boundaries are scheduling barriers: every CC write (the
// PUSHC/SETCC/POPC region controls) bounds the schedulable region by
// name, so no instruction crosses a lane-state change and every region
// executes under one CC state.  Each bounded sub-region here is a single
// dependence chain -- the list scheduler engages, finds no modeled
// makespan decrease, and keeps the original order byte-identically (the
// named no-win refusal).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "List-schedule barrier: cc-write uid=\\d+" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "List-schedule refused: no modeled makespan decrease" 2 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "List-schedule: bb" "rvtt_schedule" } }

void cc_barrier_splits_regions ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto p = __builtin_rvtt_sfpreadlreg (1);
  auto q = __builtin_rvtt_sfpreadlreg (2);
  auto b = __builtin_rvtt_sfpreadlreg (3);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  p = __builtin_rvtt_sfpmad (p, x, p, 0);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc_v (b, 0);
  q = __builtin_rvtt_sfpmul (q, x, 0);
  q = __builtin_rvtt_sfpmul (q, x, 0);
  q = __builtin_rvtt_sfpmul (q, x, 0);
  __builtin_rvtt_sfppopc (0);
  __builtin_rvtt_sfpwritelreg (p, 1);
  __builtin_rvtt_sfpwritelreg (q, 2);
}
