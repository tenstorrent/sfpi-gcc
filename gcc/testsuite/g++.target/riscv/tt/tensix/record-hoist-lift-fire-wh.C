// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-optimize-record-hoist-lift -fdump-rtl-rvtt_replay -fdump-rtl-rvtt_dst_autoincr" }
// Check that the placement lift moves the record to the outer loop preheader on Wormhole, and dst-autoincr forms its group.
// { dg-final { scan-rtl-dump "record-hoist-lift: lifted placement to bb \[0-9\]+ .1 level.s. out" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist: invariant re-record window admitted .trips 11, words 7, benefit 7417." "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "record-hoist-downstream-fallback-unprofitable" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Capturing and executing sequence" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Dst-autoincr group: bb \[0-9\]+ rows 1 stride 2 config 3 words" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "mod-write-noexec-record-composition-unaudited" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 7, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 7, 0, 0" 2 } }
void tile_rows_lift_fire_wh ()
{
  auto ga = __builtin_rvtt_sfpreadlreg (2);
  auto gb = __builtin_rvtt_sfpreadlreg (3);
  auto gc = __builtin_rvtt_sfpreadlreg (6);
  for (unsigned tx = 0; tx != 4; ++tx)
    {
      /* Eleven dynamic rows strictly repay setup plus live-crossing cost.  */
      for (unsigned kx = 0; kx != 11; ++kx)
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
	  __builtin_rvtt_sfpstore (nullptr, gc, 0, 0, 0, 0, 3);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
  __builtin_rvtt_sfpwritelreg (ga, 2);
  __builtin_rvtt_sfpwritelreg (gb, 3);
  __builtin_rvtt_sfpwritelreg (gc, 6);
}
