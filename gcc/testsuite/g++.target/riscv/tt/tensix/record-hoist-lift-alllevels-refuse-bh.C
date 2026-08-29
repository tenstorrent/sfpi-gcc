// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-optimize-record-hoist-lift -fdump-rtl-rvtt_replay" }
// Record-hoist placement lift, no-admissible-level near miss (lane
// IL): every reachable placement is either still within a mod-write's
// drained-frontend window or unreachable by the walk.  The mid-level
// (tx) body carries its OWN would-be mod-write row after the inner
// loop, so the level-1 placement (the tx preheader) is dirty across
// the mx backedge and the walk continues; crossing the outermost (mx)
// loop then refuses -- its tail holds pointer-parameter volatile
// stores whose address is not provably outside the instruction FIFO
// (a possible push of a REPLAY word).  No oracle-clean level exists:
// the composition refusal stands by name and the in-body
// exec-while-record formation keeps today's bytes.
// { dg-final { scan-rtl-dump "record-hoist-lift: level 1 placement bb \[0-9\]+ still within a mod-write drained-frontend window" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist-lift: level stop at loop \[0-9\]+" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist-lift refused: record-hoist-lift-no-admissible-level" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist refused: record-hoist-downstream-fallback-unprofitable" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
void tile_rows_lift_alllevels_refuse (volatile int *sep, int salt)
{
  auto ga = __builtin_rvtt_sfpreadlreg (2);
  auto gb = __builtin_rvtt_sfpreadlreg (3);
  auto gc = __builtin_rvtt_sfpreadlreg (6);
  for (unsigned mx = 0; mx != 5; ++mx)
    {
      for (unsigned tx = 0; tx != 4; ++tx)
	{
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
	      __builtin_rvtt_sfpstore (nullptr, gc, 0, 0, 0, 0, 7);
	      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	    }
	  /* The tx body's own would-be mod-write row: dirties the
	     level-1 placement across the mx backedge.  */
	  gb = __builtin_rvtt_sfpmul (gb, gb, 0);
	  __builtin_rvtt_sfpstore (nullptr, gb, 0, 0, 0, 0, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
      /* Pointer-parameter volatile stores: possible FIFO pushes, so
	 crossing the mx loop refuses fail-closed.  */
      sep[0] = salt; sep[1] = salt + 1;
    }
  __builtin_rvtt_sfpwritelreg (ga, 2);
  __builtin_rvtt_sfpwritelreg (gb, 3);
  __builtin_rvtt_sfpwritelreg (gc, 6);
}
