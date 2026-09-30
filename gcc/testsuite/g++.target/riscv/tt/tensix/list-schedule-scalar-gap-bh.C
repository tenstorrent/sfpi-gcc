// Entry-boundary discipline across a scalar gap: the DYNAMIC pad probe
// skips non-Tensix insns, so the SFPU producer BEFORE the volatile
// scalar store is still word-adjacent to the region's first member.
// The entry-producer walk uses the probe's own vocabulary: the audited
// mad-family producer reaches across the scalar and floors its reader
// at slot 1, so the baseline models 8 slots (reader-first stalls the
// entry adjacency) and the candidate 6 -- the exact "8 -> 6" only
// holds when the floor crosses the gap (a bypassed entry would model
// "7 -> 6"), which is the DU-S6 witness.
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
