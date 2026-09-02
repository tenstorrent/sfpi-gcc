// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-optimize-record-hoist-lift -fdump-rtl-rvtt_replay -fdump-rtl-rvtt_dst_autoincr" }
// Record-hoist placement lift, UNCOUNTED (data-dependent trips) fire
//.  The inner row loop's trip count is a runtime parameter:
// the record-hoist admits it under the structural trips>=1
// fact and the 2-trip break-even, the downstream-fallback oracle
// refuses the inner preheader (the row's would-be mod-write store
// reaches it across the backedge), and the lift walks out to the
// outer dedicated preheader whose upstream paths reach the function
// entry.  The loop CONTROL -- the data-dependent exit test -- stays
// in the body and executes per trip; only the record pass moves.
// Pricing: per_trip = 6x123 - 70 = 668; record_once = 7x123 + 300 =
// 1161; 2-trip benefit = 2x668 - 1161 = 175 >= 60.
// { dg-final { scan-rtl-dump "record-hoist-lift: lifted placement to bb \[0-9\]+ .1 level.s. out" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist: runtime-trip re-record window admitted .structural trips>=1, words 6, 2-trip benefit 175, single-trip exposure 493." "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "record-hoist-downstream-fallback-unprofitable" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Capturing and executing sequence" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 0" 2 } }
void tile_rows_lift_uncounted (unsigned rows)
{
  auto ga = __builtin_rvtt_sfpreadlreg (2);
  auto gb = __builtin_rvtt_sfpreadlreg (3);
  auto gc = __builtin_rvtt_sfpreadlreg (6);
  for (unsigned tx = 0; tx != 4; ++tx)
    {
      /* do-while spelling: no zero-trip guard, so the outer loop keeps
	 a dedicated (single-successor) preheader for the walk.  */
      unsigned kx = 0;
      do
	{
	  ga = __builtin_rvtt_sfpmul (ga, ga, 0);
	  gb = __builtin_rvtt_sfpmul (gb, gb, 0);
	  gc = __builtin_rvtt_sfpmul (gc, gc, 0);
	  ga = __builtin_rvtt_sfpmul (ga, gb, 0);
	  gb = __builtin_rvtt_sfpmul (gb, gc, 0);
	  gc = __builtin_rvtt_sfpmul (gc, ga, 0);
	  ga = __builtin_rvtt_sfpmul (gb, gc, 0); /* clone separator */
	  ga = __builtin_rvtt_sfpmul (ga, ga, 0);
	  gb = __builtin_rvtt_sfpmul (gb, gb, 0);
	  gc = __builtin_rvtt_sfpmul (gc, gc, 0);
	  ga = __builtin_rvtt_sfpmul (ga, gb, 0);
	  gb = __builtin_rvtt_sfpmul (gb, gc, 0);
	  gc = __builtin_rvtt_sfpmul (gc, ga, 0);
	  /* Distinct-immediate pad: keeps the post-hoist trip's own issue
	     words covering the inner backedge crossing (the EB/EQ crossing
	     rule is not under test here).  */
	  gc = __builtin_rvtt_sfpaddi (nullptr, gc, 0x3e91, 0, 0, 0);
	  gc = __builtin_rvtt_sfpaddi (nullptr, gc, 0x3e92, 0, 0, 0);
	  __builtin_rvtt_sfpstore (nullptr, gc, 0, 0, 0, 0, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
      while (++kx != rows);
    }
  __builtin_rvtt_sfpwritelreg (ga, 2);
  __builtin_rvtt_sfpwritelreg (gb, 3);
  __builtin_rvtt_sfpwritelreg (gc, 6);
}
