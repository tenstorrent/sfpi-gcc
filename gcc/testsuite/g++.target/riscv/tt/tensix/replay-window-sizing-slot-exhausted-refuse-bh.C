// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-optimize-replay-window-sizing -fdump-rtl-rvtt_replay" }
// Check that window sizing refuses by name when the wider window does not fit the free replay slots, keeping the 6-word window.
// { dg-final { scan-rtl-dump-times "window-sizing: widened" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump "window-sizing refused: window-sizing-slot-exhausted: every wider same-anchor candidate exceeds the free slot span 8; keeping the picked .\[0-9\]+,\\+6. window" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoisted no-exec capture .0,\\+24. to preheader bb \[0-9\]+; 2 playbacks" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoisted no-exec capture .24,\\+6. to preheader bb \[0-9\]+; 9 playbacks" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 24, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 24, 0, 0" 2 } }
// { dg-final { scan-assembler-times "TTREPLAY\t24, 6, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t24, 6, 0, 0" 9 } }

#define UNIT()                                  \
  ga = __builtin_rvtt_sfpmul (ga, ga, 0);       \
  gb = __builtin_rvtt_sfpmul (gb, gb, 0);       \
  gc = __builtin_rvtt_sfpmul (gc, gc, 0);       \
  ga = __builtin_rvtt_sfpmul (ga, gb, 0);       \
  gb = __builtin_rvtt_sfpmul (gb, gc, 0);       \
  gc = __builtin_rvtt_sfpmul (gc, ga, 0)

#define RUN()                                   \
  UNIT ();                                      \
  UNIT ();                                      \
  UNIT ();                                      \
  UNIT ();                                      \
  UNIT ();                                      \
  UNIT ();                                      \
  UNIT ();                                      \
  UNIT ();                                      \
  UNIT ();                                      \
  ga = __builtin_rvtt_sfpmul (ga, ga, 0);       \
  gb = __builtin_rvtt_sfpmul (gb, gb, 0);       \
  gc = __builtin_rvtt_sfpmul (gc, gc, 0)

void tile_rows_window_sizing_slot_exhausted ()
{
  auto ga = __builtin_rvtt_sfpreadlreg (2);
  auto gb = __builtin_rvtt_sfpreadlreg (3);
  auto gc = __builtin_rvtt_sfpreadlreg (6);
  for (unsigned tx = 0; tx != 11; ++tx)
    {
      RUN ();
    }
  for (unsigned ux = 0; ux != 5; ++ux)
    {
      RUN ();
    }
  __builtin_rvtt_sfpwritelreg (ga, 2);
  __builtin_rvtt_sfpwritelreg (gb, 3);
  __builtin_rvtt_sfpwritelreg (gc, 6);
}
