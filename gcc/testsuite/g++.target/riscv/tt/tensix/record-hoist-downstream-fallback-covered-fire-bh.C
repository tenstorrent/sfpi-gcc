// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_replay -fdump-rtl-rvtt_dst_autoincr" }
// Downstream-fallback composition pricing, covered side: the
// SAME re-record loop and would-be mod-write row as the refuse twin,
// but with twelve scalar separator words on the only path from the row
// back to the inner preheader (the outer-loop tail).  The planned
// capture is >= W_drain issue words from the row, the mirror admits the
// hoist, and the dst-autoincr group guard then admits the SAME audited
// distance at pass 397: BOTH transforms fire and compose -- proving the
// refusal keys the audited window, not the passes' co-presence.
// { dg-final { scan-rtl-dump "record-hoist: invariant re-record window admitted" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "record-hoist-downstream-fallback-unprofitable" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Dst-autoincr group: bb \[0-9\]+ rows 1 stride 2 config 3 words" "rvtt_dst_autoincr" } }
// { dg-final { scan-rtl-dump-not "mod-write-noexec-record-composition-unaudited" "rvtt_dst_autoincr" } }
// { dg-final { scan-assembler-not "TTINCRWC" } }
// { dg-final { scan-assembler "TTREPLAY\t0, 6, 0, 1" } }
void tile_rows_fallback_covered (volatile int *sep)
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
	  /* Distinct-immediate pad: keeps the post-hoist iteration's own
	     issue words >= W_drain so the inner backedge crossing stays
	     covered (the EB/EQ crossing rule is not under test here).  */
	  gc = __builtin_rvtt_sfpaddi (nullptr, gc, 0x3e81, 0, 0, 0);
	  gc = __builtin_rvtt_sfpaddi (nullptr, gc, 0x3e82, 0, 0, 0);
	  gc = __builtin_rvtt_sfpaddi (nullptr, gc, 0x3e83, 0, 0, 0);
	  gc = __builtin_rvtt_sfpaddi (nullptr, gc, 0x3e84, 0, 0, 0);
	  gc = __builtin_rvtt_sfpaddi (nullptr, gc, 0x3e85, 0, 0, 0);
	  __builtin_rvtt_sfpstore (nullptr, gc, 0, 0, 0, 0, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
      sep[0] = 11; sep[1] = 22; sep[2] = 33; sep[3] = 44;
      sep[4] = 55; sep[5] = 66; sep[6] = 77; sep[7] = 88;
      sep[8] = 99; sep[9] = 110; sep[10] = 121; sep[11] = 132;
    }
  __builtin_rvtt_sfpwritelreg (ga, 2);
  __builtin_rvtt_sfpwritelreg (gb, 3);
  __builtin_rvtt_sfpwritelreg (gc, 6);
}
