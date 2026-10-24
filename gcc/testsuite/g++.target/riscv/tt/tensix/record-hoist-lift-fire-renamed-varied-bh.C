// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-optimize-record-hoist-lift -fdump-rtl-rvtt_replay -fdump-rtl-rvtt_dst_autoincr" }
// Same as record-hoist-lift-fire-bh.C with different names, constants and trip count.
// { dg-final { scan-rtl-dump "record-hoist-lift: lifted placement to bb \[0-9\]+ .1 level.s. out" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist: invariant re-record window admitted .trips 9, words 6, benefit 4851." "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "record-hoist-downstream-fallback-unprofitable" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Capturing and executing sequence" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist-enabled no-exec capture dominates explicit mod-write group" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "Dst-autoincr group: bb" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-times "TTINCRWC\t0, 2, 0, 0" 1 } }
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
