// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-dst-autoincr -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-optimize-record-hoist-lift -fdump-rtl-rvtt_replay" }
// Record-hoist placement lift, walk-stop-at-first-level near miss
//: the outer loop's tail carries pointer-parameter volatile
// stores whose address is not provably outside the instruction FIFO
// (a possible push of a REPLAY word that could re-record the lifted
// slots between the record and a later trip's launch), so the
// replay-preservation audit refuses the only crossable loop and the
// walk stops with zero admissible levels.  The composition refusal
// stands by name and today's in-body formation keeps its bytes.
// { dg-final { scan-rtl-dump "record-hoist-lift: level stop at loop \[0-9\]+" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist-lift refused: record-hoist-lift-no-admissible-level: no oracle-clean admissible placement .walked 0 level.s.." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "record-hoist refused: record-hoist-downstream-fallback-unprofitable" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Capturing and executing sequence" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Hoisted no-exec capture" "rvtt_replay" } }
void tile_rows_lift_loopaudit_stop (volatile int *sep, int salt)
{
  auto ga = __builtin_rvtt_sfpreadlreg (2);
  auto gb = __builtin_rvtt_sfpreadlreg (3);
  auto gc = __builtin_rvtt_sfpreadlreg (6);
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
      /* Pointer-parameter volatile stores: possible FIFO pushes.  */
      sep[0] = salt; sep[1] = salt + 1;
    }
  __builtin_rvtt_sfpwritelreg (ga, 2);
  __builtin_rvtt_sfpwritelreg (gb, 3);
  __builtin_rvtt_sfpwritelreg (gc, 6);
}
