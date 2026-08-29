// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-optimize-record-hoist-lift -fdump-rtl-rvtt_replay -fdump-rtl-rvtt_dst_autoincr" }
// Record-hoist placement lift, fire side, renamed-varied twin (DG2 pass-generality discipline; the lcm-fresh
// window-density shape, rvtt-cost.md "RECORD-HOIST PLACEMENT LIFT").
// The SAME re-record nest as the downstream-fallback refuse twin: the
// inner preheader placement sits within the audited drained-frontend
// window of the row's would-be mod-write store across the outer
// backedge (distance 3 < 7), so the plain hoist refuses.  Under the
// lift flag the placement walks outward: the outer loop proves
// replay-preserving, its dedicated preheader's every upstream path
// reaches the function entry (oracle-clean), and the UNCHANGED no-exec
// hoist commits there -- the record is delivered once per kernel entry
// (the hand init-record discipline) instead of re-recorded per row,
// and dst-autoincr keeps its mod-write fire (the guard's dominating
// non-reachable deliverer class).
// Pricing floor (immediate loop): 9 trips x (6x123 - 70) - (7x123 +
// 300) = 4851 >= 60.
// { dg-final { scan-rtl-dump "record-hoist-lift: lifted placement to bb \[0-9\]+ .1 level.s. out" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist: invariant re-record window admitted .trips 9, words 6, benefit 4851." "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "record-hoist-downstream-fallback-unprofitable" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Capturing and executing sequence" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Dst-autoincr group: bb \[0-9\]+ rows 1 stride 2 config 3 words" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "mod-write-noexec-record-composition-unaudited" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 0" 2 } }
void vault_rows_lift_fire_v ()
{
  auto qa = __builtin_rvtt_sfpreadlreg (1);
  auto qb = __builtin_rvtt_sfpreadlreg (4);
  auto qc = __builtin_rvtt_sfpreadlreg (5);
  for (unsigned tx = 0; tx != 3; ++tx)
    {
      /* Nine dynamic rows strictly repay setup plus live-crossing cost.  */
      for (unsigned kx = 0; kx != 9; ++kx)
	{
	  qa = __builtin_rvtt_sfpmul (qa, qa, 0);
	  qb = __builtin_rvtt_sfpmul (qb, qb, 0);
	  qc = __builtin_rvtt_sfpmul (qc, qc, 0);
	  qa = __builtin_rvtt_sfpmul (qa, qb, 0);
	  qb = __builtin_rvtt_sfpmul (qb, qc, 0);
	  qc = __builtin_rvtt_sfpmul (qc, qa, 0);
	  qa = __builtin_rvtt_sfpmul (qb, qc, 0); /* clone separator */
	  qa = __builtin_rvtt_sfpmul (qa, qa, 0);
	  qb = __builtin_rvtt_sfpmul (qb, qb, 0);
	  qc = __builtin_rvtt_sfpmul (qc, qc, 0);
	  qa = __builtin_rvtt_sfpmul (qa, qb, 0);
	  qb = __builtin_rvtt_sfpmul (qb, qc, 0);
	  qc = __builtin_rvtt_sfpmul (qc, qa, 0);
	  /* Distinct-immediate pad: keeps the post-hoist trip's own issue
	     words covering the inner backedge crossing (the EB/EQ crossing
	     rule is not under test here).  */
	  qc = __builtin_rvtt_sfpaddi (nullptr, qc, 0x3e91, 0, 0, 0);
	  qc = __builtin_rvtt_sfpaddi (nullptr, qc, 0x3e92, 0, 0, 0);
	  __builtin_rvtt_sfpstore (nullptr, qc, 0, 0, 0, 0, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
  __builtin_rvtt_sfpwritelreg (qa, 1);
  __builtin_rvtt_sfpwritelreg (qb, 4);
  __builtin_rvtt_sfpwritelreg (qc, 5);
}
