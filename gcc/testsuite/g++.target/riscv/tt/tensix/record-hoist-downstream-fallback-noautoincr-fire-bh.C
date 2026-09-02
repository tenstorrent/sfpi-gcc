// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_replay" }
// Downstream-fallback composition pricing, pass-gate control:
// the SAME shape as the refuse twin but with the dst-autoincr pass
// DISABLED: no transform is forthcoming in either world, the would-be
// row stays an explicit increment in both, the streams-identical
// premise holds, and the hoist must fire -- the mirror is gated on the
// composition actually being reachable, not on the row's presence.
// { dg-final { scan-rtl-dump "record-hoist: invariant re-record window admitted" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoisted no-exec capture" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "record-hoist-downstream-fallback-unprofitable" "rvtt_replay" } }
// { dg-final { scan-assembler "TTINCRWC" } }
// { dg-final { scan-assembler "TTREPLAY\t0, 6, 0, 1" } }
void tile_rows_fallback_noautoincr ()
{
  auto ga = __builtin_rvtt_sfpreadlreg (2);
  auto gb = __builtin_rvtt_sfpreadlreg (3);
  auto gc = __builtin_rvtt_sfpreadlreg (6);
  for (unsigned tx = 0; tx != 4; ++tx)
    {
      /* Keep the same eleven-row shape as the pass-enabled twins.  */
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
    }
  __builtin_rvtt_sfpwritelreg (ga, 2);
  __builtin_rvtt_sfpwritelreg (gb, 3);
  __builtin_rvtt_sfpwritelreg (gc, 6);
}
