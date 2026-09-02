// Scheduler arsenal: pressure-aware dispatch at the architectural
// eight-LREG boundary.
//
// fn 1 (pressure_fit_7): seven independent two-mad chains plus the
// shared multiplicand x.  All eight allocatable LREGs are live from
// entry to the writebacks, so EVERY schedule of this region runs at
// all eight hard LREGs concurrently live -- the architectural boundary.
// The serial emission carries one modeled stall inside each chain
// (audited mad-family latency 1); the seven-wide interleave hides all
// of them.  The scheduler must choose the fitting schedule at the
// architectural boundary (all eight hard LREGs live across the
// interleave).  Post-allocation the eight names themselves are the
// pressure bound (DU-S3): there is no pressure gate to report, and
// no "pressure" refusal line may ever appear
// ("pressure ... exceeds").
//
// fn 2 (pressure_reuse_8): an EIGHTH chain whose accumulator has no
// ninth register to live in.  The unconstrained latency-optimal
// schedule -- all eight chains interleaved abreast -- needs
// 8 accumulators + x = 9 simultaneously live values and is therefore
// unschedulable in the eight-register file; the register allocator's
// reuse (the eighth chain rides a register freed by an earlier chain's
// writeback) expresses exactly that as WAW/WAR issue-distance edges in
// the region DAG.  The pressure-aware outcome is the FITTING schedule:
// whatever interleave the reuse edges admit, never a candidate above
// the file (the makespan oracle documents the gap: virtual-register
// lower bound < achieved == reuse-constrained lower bound).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-list-schedule -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// MEASURED (hand-verified, see the recorded oracle
// evidence): fn 1 baseline 22 = 14 words + 7 serial chain stalls +
// 1 block-end drain; seven-wide interleave = 14 words + drain = 15 =
// the critical-path lower bound.  fn 2: 15 nodes (the c1 sub-chain is
// cut off by its writeback marker's region flush), same 22 -> 15, and
// the emitted stream keeps c1's L1 writeback before c8's L1 reuse.
// [post-scheduler-hardening adjudication] fn2 baseline is now 23 (was 22): a
// entry-producer walk reaches across the zero-length markers to the
// real audited producer, so the boundary stall the original oracle counted
// (finding 5) is now IN the region model -- same optimum, honest base.
// { dg-final { scan-rtl-dump-times "List-schedule: bb \\d+ nodes=14 makespan 22 -> 15 target=bh" 1 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-times "List-schedule: bb \\d+ nodes=15 makespan 23 -> 15 target=bh" 1 "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "List-schedule refused: pressure" "rvtt_schedule" } }

void pressure_fit_7 ()
{
  auto x  = __builtin_rvtt_sfpreadlreg (0);
  auto c1 = __builtin_rvtt_sfpreadlreg (1);
  auto c2 = __builtin_rvtt_sfpreadlreg (2);
  auto c3 = __builtin_rvtt_sfpreadlreg (3);
  auto c4 = __builtin_rvtt_sfpreadlreg (4);
  auto c5 = __builtin_rvtt_sfpreadlreg (5);
  auto c6 = __builtin_rvtt_sfpreadlreg (6);
  auto c7 = __builtin_rvtt_sfpreadlreg (7);
  c1 = __builtin_rvtt_sfpmad (c1, x, c1, 0);
  c1 = __builtin_rvtt_sfpmad (c1, x, c1, 0);
  c2 = __builtin_rvtt_sfpmad (c2, x, c2, 0);
  c2 = __builtin_rvtt_sfpmad (c2, x, c2, 0);
  c3 = __builtin_rvtt_sfpmad (c3, x, c3, 0);
  c3 = __builtin_rvtt_sfpmad (c3, x, c3, 0);
  c4 = __builtin_rvtt_sfpmad (c4, x, c4, 0);
  c4 = __builtin_rvtt_sfpmad (c4, x, c4, 0);
  c5 = __builtin_rvtt_sfpmad (c5, x, c5, 0);
  c5 = __builtin_rvtt_sfpmad (c5, x, c5, 0);
  c6 = __builtin_rvtt_sfpmad (c6, x, c6, 0);
  c6 = __builtin_rvtt_sfpmad (c6, x, c6, 0);
  c7 = __builtin_rvtt_sfpmad (c7, x, c7, 0);
  c7 = __builtin_rvtt_sfpmad (c7, x, c7, 0);
  __builtin_rvtt_sfpwritelreg (c1, 1);
  __builtin_rvtt_sfpwritelreg (c2, 2);
  __builtin_rvtt_sfpwritelreg (c3, 3);
  __builtin_rvtt_sfpwritelreg (c4, 4);
  __builtin_rvtt_sfpwritelreg (c5, 5);
  __builtin_rvtt_sfpwritelreg (c6, 6);
  __builtin_rvtt_sfpwritelreg (c7, 7);
}

void pressure_reuse_8 ()
{
  auto x  = __builtin_rvtt_sfpreadlreg (0);
  auto c1 = __builtin_rvtt_sfpreadlreg (1);
  auto c2 = __builtin_rvtt_sfpreadlreg (2);
  auto c3 = __builtin_rvtt_sfpreadlreg (3);
  auto c4 = __builtin_rvtt_sfpreadlreg (4);
  auto c5 = __builtin_rvtt_sfpreadlreg (5);
  auto c6 = __builtin_rvtt_sfpreadlreg (6);
  auto c7 = __builtin_rvtt_sfpreadlreg (7);
  c1 = __builtin_rvtt_sfpmad (c1, x, c1, 0);
  c1 = __builtin_rvtt_sfpmad (c1, x, c1, 0);
  __builtin_rvtt_sfpwritelreg (c1, 1);
  /* Chain 8: its accumulator can only live in a register freed by an
     earlier chain's writeback -- the reuse edge that caps interleave.  */
  auto c8 = __builtin_rvtt_sfpmad (x, x, x, 0);
  c8 = __builtin_rvtt_sfpmad (c8, x, c8, 0);
  c2 = __builtin_rvtt_sfpmad (c2, x, c2, 0);
  c2 = __builtin_rvtt_sfpmad (c2, x, c2, 0);
  c3 = __builtin_rvtt_sfpmad (c3, x, c3, 0);
  c3 = __builtin_rvtt_sfpmad (c3, x, c3, 0);
  c4 = __builtin_rvtt_sfpmad (c4, x, c4, 0);
  c4 = __builtin_rvtt_sfpmad (c4, x, c4, 0);
  c5 = __builtin_rvtt_sfpmad (c5, x, c5, 0);
  c5 = __builtin_rvtt_sfpmad (c5, x, c5, 0);
  c6 = __builtin_rvtt_sfpmad (c6, x, c6, 0);
  c6 = __builtin_rvtt_sfpmad (c6, x, c6, 0);
  c7 = __builtin_rvtt_sfpmad (c7, x, c7, 0);
  c7 = __builtin_rvtt_sfpmad (c7, x, c7, 0);
  __builtin_rvtt_sfpwritelreg (c8, 0);
  __builtin_rvtt_sfpwritelreg (c2, 2);
  __builtin_rvtt_sfpwritelreg (c3, 3);
  __builtin_rvtt_sfpwritelreg (c4, 4);
  __builtin_rvtt_sfpwritelreg (c5, 5);
  __builtin_rvtt_sfpwritelreg (c6, 6);
  __builtin_rvtt_sfpwritelreg (c7, 7);
}
