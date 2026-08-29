// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-optimize-replay-window-sizing -fdump-rtl-rvtt_replay" }
// Exact-multiple widening (lane IM): the run is exactly 4 units (24
// words, no tail), so the widened 12-word window covers it with 2 full
// launches and NO partial trim -- the partial-playback path must stay
// silent (a trim only forms for a genuine prefix remainder).
// { dg-final { scan-rtl-dump "window-sizing: widened .\[0-9\]+,\\+6. x4 .covering 24 words. to .\[0-9\]+,\\+12. x2 \\+ 0-word prefix trim .per-trip deliveries 4 -> 2." "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "partial playback" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoisted no-exec capture .0,\\+12. to preheader bb \[0-9\]+; 2 playbacks" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 12, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 12, 0, 0" 2 } }

#define UNIT()                                  \
  ga = __builtin_rvtt_sfpmul (ga, ga, 0);       \
  gb = __builtin_rvtt_sfpmul (gb, gb, 0);       \
  gc = __builtin_rvtt_sfpmul (gc, gc, 0);       \
  ga = __builtin_rvtt_sfpmul (ga, gb, 0);       \
  gb = __builtin_rvtt_sfpmul (gb, gc, 0);       \
  gc = __builtin_rvtt_sfpmul (gc, ga, 0)

void tile_rows_window_sizing_no_trim ()
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
    }
  __builtin_rvtt_sfpwritelreg (ga, 2);
  __builtin_rvtt_sfpwritelreg (gb, 3);
  __builtin_rvtt_sfpwritelreg (gc, 6);
}
