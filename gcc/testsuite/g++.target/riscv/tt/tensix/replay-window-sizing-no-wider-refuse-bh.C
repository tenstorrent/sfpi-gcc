// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-optimize-replay-window-sizing -fdump-rtl-rvtt_replay" }
// No-wider-candidate near miss: the run is exactly 3 units
// (18 words, no tail), so no wider same-anchor candidate exists at all
// (every longer window has a single non-overlapping instance and is
// never an active candidate) -- window sizing refuses by name and the
// picked 6-word window keeps today's hoisted shape byte-identically
// (3 launches).
// { dg-final { scan-rtl-dump "window-sizing refused: window-sizing-no-wider-candidate: no admissible wider same-anchor candidate; keeping the picked .\[0-9\]+,\\+6. window" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "window-sizing: widened" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Hoisted no-exec capture .0,\\+6. to preheader bb \[0-9\]+; 3 playbacks" "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 6, 0, 0" 3 } }

#define UNIT()                                  \
  ga = __builtin_rvtt_sfpmul (ga, ga, 0);       \
  gb = __builtin_rvtt_sfpmul (gb, gb, 0);       \
  gc = __builtin_rvtt_sfpmul (gc, gc, 0);       \
  ga = __builtin_rvtt_sfpmul (ga, gb, 0);       \
  gb = __builtin_rvtt_sfpmul (gb, gc, 0);       \
  gc = __builtin_rvtt_sfpmul (gc, ga, 0)

void tile_rows_window_sizing_no_wider ()
{
  auto ga = __builtin_rvtt_sfpreadlreg (2);
  auto gb = __builtin_rvtt_sfpreadlreg (3);
  auto gc = __builtin_rvtt_sfpreadlreg (6);
  for (unsigned tx = 0; tx != 11; ++tx)
    {
      UNIT ();
      UNIT ();
      UNIT ();
    }
  __builtin_rvtt_sfpwritelreg (ga, 2);
  __builtin_rvtt_sfpwritelreg (gb, 3);
  __builtin_rvtt_sfpwritelreg (gc, 6);
}
