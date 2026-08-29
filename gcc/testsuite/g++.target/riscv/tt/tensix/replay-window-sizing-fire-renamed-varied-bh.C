// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-optimize-replay-window-sizing -fdump-rtl-rvtt_replay" }
// Renamed/varied companion of replay-window-sizing-fire-bh.C: different
// LREG homes, different op mix inside the unit, different trip count --
// same widened 24-word window with 2 full launches + one partial
// prefix-trim playback per trip.
// { dg-final { scan-rtl-dump "window-sizing: widened .\[0-9\]+,\\+6. x9 .covering 54 words. to .\[0-9\]+,\\+24. x2 \\+ \[0-9\]-word prefix trim .per-trip deliveries 9 -> 3." "rvtt_replay" } }
// { dg-final { scan-rtl-dump "is a recorded-window prefix; launching .0,\\+\[0-9\]. as a partial playback" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoisted no-exec capture .0,\\+24. to preheader bb \[0-9\]+; 2 playbacks" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Capturing and executing sequence" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 24, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 24, 0, 0" 2 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, \[0-9\], 0, 0" 1 } }

#define UNIT()                                  \
  va = __builtin_rvtt_sfpmul (va, vb, 0);       \
  vb = __builtin_rvtt_sfpmul (vb, vb, 0);       \
  vc = __builtin_rvtt_sfpmul (vc, va, 0);       \
  va = __builtin_rvtt_sfpmul (va, va, 0);       \
  vb = __builtin_rvtt_sfpmul (vb, vc, 0);       \
  vc = __builtin_rvtt_sfpmul (vc, vc, 0)

void rows_window_sizing_fire_varied ()
{
  auto va = __builtin_rvtt_sfpreadlreg (1);
  auto vb = __builtin_rvtt_sfpreadlreg (4);
  auto vc = __builtin_rvtt_sfpreadlreg (5);
  for (unsigned rx = 0; rx != 7; ++rx)
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
      va = __builtin_rvtt_sfpmul (va, vb, 0);
      vb = __builtin_rvtt_sfpmul (vb, vb, 0);
      vc = __builtin_rvtt_sfpmul (vc, va, 0);
    }
  __builtin_rvtt_sfpwritelreg (va, 1);
  __builtin_rvtt_sfpwritelreg (vb, 4);
  __builtin_rvtt_sfpwritelreg (vc, 5);
}
