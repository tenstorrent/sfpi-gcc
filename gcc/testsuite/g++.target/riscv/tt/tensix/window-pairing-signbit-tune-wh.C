// Window-pairing on the frozen signbit shape (frozen
// whole-word shift/cast program, launch-only rows, drain 3): the exact
// model tunes the inter-row spacing to TWO NOPs -- the follower launch
// then issues AT the last pending retirement cycle, the established
// front-end equality admission (retire-before-issue; the same rule the
// drain-schedule fixedvd-face-equality twin pins at run boundaries) --
// and names the LREG overlap that binds it from below (at one NOP
// fewer the follower's own staged events land inside the horizon on
// the launch register).  The lane-EV corrupting shape (back-to-back,
// zero spacing) stays refused by the same overlap.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-window-pairing -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner drain-interrow: drain=3 rows=8" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner window-pairing: interrow-drain 3 -> 2 rows=8 bound=window-pairing-lreg-overlap" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPNOP" 17 } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 3);          \
      auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31, 0, 0, 0);\
      auto converted = __builtin_rvtt_sfpcast (shifted, 0);                   \
      __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0, 3);            \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void signbit_rows ()
{
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
}

#undef ROW
