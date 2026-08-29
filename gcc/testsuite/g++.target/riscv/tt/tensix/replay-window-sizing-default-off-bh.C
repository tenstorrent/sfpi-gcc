// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -fdump-rtl-rvtt_replay" }
// Default-off control for replay-window-sizing-fire-bh.C: without
// -mtt-tensix-optimize-replay-window-sizing the same shape keeps
// today's bytes -- the in-block key's 6-word window, hoisted, with 9
// full launches per trip and the 3-word tail inline; no widening, no
// partial playback.
// { dg-final { scan-rtl-dump-not "window-sizing: widened" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "window-sizing refused" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "partial playback" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoisted no-exec capture .0,\\+6. to preheader bb \[0-9\]+; 9 playbacks" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 0" 9 } }
// { dg-final { scan-assembler-not "TTREPLAY\t0, 24" } }

#define UNIT()                                  \
  ga = __builtin_rvtt_sfpmul (ga, ga, 0);       \
  gb = __builtin_rvtt_sfpmul (gb, gb, 0);       \
  gc = __builtin_rvtt_sfpmul (gc, gc, 0);       \
  ga = __builtin_rvtt_sfpmul (ga, gb, 0);       \
  gb = __builtin_rvtt_sfpmul (gb, gc, 0);       \
  gc = __builtin_rvtt_sfpmul (gc, ga, 0)

void tile_rows_window_sizing_default_off ()
{
  auto ga = __builtin_rvtt_sfpreadlreg (2);
  auto gb = __builtin_rvtt_sfpreadlreg (3);
  auto gc = __builtin_rvtt_sfpreadlreg (6);
  for (unsigned tx = 0; tx != 11; ++tx)
    {
      UNIT ();
      UNIT ();
      UNIT ();
      UNIT ();
      UNIT ();
      UNIT ();
      UNIT ();
      UNIT ();
      UNIT ();
      /* Unit-prefix tail.  */
      ga = __builtin_rvtt_sfpmul (ga, ga, 0);
      gb = __builtin_rvtt_sfpmul (gb, gb, 0);
      gc = __builtin_rvtt_sfpmul (gc, gc, 0);
    }
  __builtin_rvtt_sfpwritelreg (ga, 2);
  __builtin_rvtt_sfpwritelreg (gb, 3);
  __builtin_rvtt_sfpwritelreg (gc, 6);
}
