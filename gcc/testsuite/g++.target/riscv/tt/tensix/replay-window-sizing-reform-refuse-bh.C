// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-optimize-post-autoincr-window -mtt-tensix-optimize-replay-window-sizing -fdump-rtl-rvtt_replay_reform" }
// Reform-composition refusal (lane IM): under
// -mtt-tensix-optimize-post-autoincr-window the function's only replay
// formation runs as the re-formation pass (lane IH), whose carried
// launch-arithmetic audit is derived for FULL-length launches only --
// widening with a partial trim there is an unaudited composition, so
// window sizing refuses by name and the picked window keeps the IH
// shape byte-identically.
// { dg-final { scan-rtl-dump "window-sizing refused: window-sizing-reform-composition-unaudited" "rvtt_replay_reform" } }
// { dg-final { scan-rtl-dump-not "window-sizing: widened" "rvtt_replay_reform" } }
// { dg-final { scan-rtl-dump "Hoisted no-exec capture .0,\\+6. to preheader bb \[0-9\]+; 9 playbacks" "rvtt_replay_reform" } }
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

void tile_rows_window_sizing_reform_refuse ()
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
