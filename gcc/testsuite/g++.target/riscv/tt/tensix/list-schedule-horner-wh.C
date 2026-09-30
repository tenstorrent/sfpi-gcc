// DAG list scheduling on WH: the dual-Horner interleave eliminates the
// required-NOP pads.  Without result scoreboarding every dependent
// mad-family adjacency is a required SFPNOP site; the serial P-then-Q
// stream carries four interior pads plus the trailing writeback pad.
// The interleaved schedule leaves only the trailing pad (the final
// accumulator's writeback marker is dependent and unmovable): the
// modeled makespan counts the same slots the nop inserter pads, so the
// commit is the real issue-slot win, not a model artifact.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-times "List-schedule: bb \\d+ nodes=6 makespan 11 -> 7 target=wh" 1 "rvtt_schedule" } }
// { dg-final { scan-assembler-times "SFPNOP" 1 } }

void dual_horner_rational_wh ()
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
