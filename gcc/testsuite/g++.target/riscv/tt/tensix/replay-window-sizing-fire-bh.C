// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-optimize-replay-window-sizing -fdump-rtl-rvtt_replay" }
// Hoisted-record window sizing, fire side (lane IM; the lcm-fresh
// window-sizing shape, rvtt-cost.md "REPLAY WINDOW SIZING UNDER A
// HOISTED RECORD").  The loop body is a 6-word unit repeated 9 times
// plus a 3-word unit-prefix tail (57 words).  pick_replay's in-block
// key picks the 6-word window at 9 instances (saving 8x5-1 = 39) over
// the wider multiples (24 words x 2 non-overlapping instances: saving
// 1x23-1 = 22) -- but the record HOISTS (record-hoist admission), so
// per-trip cost is launches alone:
// the widened 24-word window delivers 2 full launches plus ONE partial
// playback of the trailing 9-word window prefix (3 issue words/trip)
// against the pick's 9 launches + 3 inline words (12).  The widened
// candidate re-proves the whole hoist admission itself; the partial
// launch is the ISA prefix-launch (REPLAY Count below the recorded
// length -- the hand gcd/lcm REPLAY(0,13) trim discipline).
// { dg-final { scan-rtl-dump "window-sizing: widened .\[0-9\]+,\\+6. x9 .covering 54 words. to .\[0-9\]+,\\+24. x2 \\+ \[0-9\]-word prefix trim .per-trip deliveries 9 -> 3." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "window-sizing: trailing \[0-9\]-word run .\[0-9\]+,\[0-9\]+. is a recorded-window prefix; launching .0,\\+\[0-9\]. as a partial playback" "rvtt_replay" } }
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

void tile_rows_window_sizing_fire ()
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
      /* Unit-prefix tail: the hand kernels' partial-launch trim.  */
      ga = __builtin_rvtt_sfpmul (ga, ga, 0);
      gb = __builtin_rvtt_sfpmul (gb, gb, 0);
      gc = __builtin_rvtt_sfpmul (gc, gc, 0);
    }
  __builtin_rvtt_sfpwritelreg (ga, 2);
  __builtin_rvtt_sfpwritelreg (gb, 3);
  __builtin_rvtt_sfpwritelreg (gc, 6);
}
