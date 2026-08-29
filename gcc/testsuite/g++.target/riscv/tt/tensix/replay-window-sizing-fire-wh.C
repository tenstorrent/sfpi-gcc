// { dg-do compile }
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-optimize-replay-window-sizing -fdump-rtl-rvtt_replay" }
// Wormhole companion of replay-window-sizing-fire-bh.C (same 6-word
// unit x9 + 3-word unit-prefix tail; sfpmul only -- single-word on
// WH): the widened 24-word window with 2 full launches + one partial
// prefix-trim playback per trip.
// { dg-final { scan-rtl-dump "window-sizing: widened .\[0-9\]+,\\+6. x9 .covering 54 words. to .\[0-9\]+,\\+24. x2 \\+ \[0-9\]-word prefix trim .per-trip deliveries 9 -> 3." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "is a recorded-window prefix; launching .0,\\+\[0-9\]. as a partial playback" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoisted no-exec capture .0,\\+24. to preheader bb \[0-9\]+; 2 playbacks" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Capturing and executing sequence" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 24, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 24, 0, 0" 2 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, \[0-9\], 0, 0" 1 } }

#define UNIT()                                  \
  ga = __builtin_rvtt_sfpmul (ga, ga, 0);       \
  gb = __builtin_rvtt_sfpmul (gb, gb, 0);       \
  gc = __builtin_rvtt_sfpmul (gc, gc, 0);       \
  ga = __builtin_rvtt_sfpmul (ga, gb, 0);       \
  gb = __builtin_rvtt_sfpmul (gb, gc, 0);       \
  gc = __builtin_rvtt_sfpmul (gc, ga, 0)

void tile_rows_window_sizing_fire_wh ()
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
