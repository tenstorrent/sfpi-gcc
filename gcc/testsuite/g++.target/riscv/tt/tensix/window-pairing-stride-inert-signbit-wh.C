// Window-pairing stride-phase generalization, WH inertness
// control: the lane-FT signbit tune shape (frozen whole-word program,
// launch-only rows, drain 3) with the stride flag ON.  The absorber
// already rides the row's last issued word, so every stride phase is
// zero and the generalized arithmetic is the established compact-form
// model verbatim: the tune stays 3 -> 2 with the same bound and the
// emitted stream is byte-identical to the flag-off form (17 SFPNOPs).
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-window-pairing -mtt-tensix-optimize-window-pairing-stride -fdump-rtl-rvtt_macro_planner" }
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
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}
