// Check that the entry-producer latency floor reaches across a scalar store to the first region member.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump-times "List-schedule: bb \\d+ nodes=4 makespan 8 -> 6 target=bh" 1 "rvtt_schedule" } }

void scalar_gap_entry (volatile unsigned *s)
{
  auto u = __builtin_rvtt_sfpreadlreg (0);
  auto v = __builtin_rvtt_sfpreadlreg (1);
  auto w = __builtin_rvtt_sfpmul (v, v, 0);	// entry producer
  *s = 1;					// scalar gap
  auto b1 = __builtin_rvtt_sfpadd (w, w, 0);	// reads the producer
  auto a1 = __builtin_rvtt_sfpmul (u, u, 0);
  auto a2 = __builtin_rvtt_sfpmul (a1, a1, 0);
  auto a3 = __builtin_rvtt_sfpmul (a2, a2, 0);
  __builtin_rvtt_sfpwritelreg (b1, 1);
  __builtin_rvtt_sfpwritelreg (a3, 0);
}
