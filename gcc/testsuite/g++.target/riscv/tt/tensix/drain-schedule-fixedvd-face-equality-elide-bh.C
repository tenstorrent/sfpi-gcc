// Boundary-keep twin: the SAME fixed-VD value-carrier region
// with the two-word CR-class face advance between runs.  The separator
// credit (2) places the next run's launch issue exactly AT the last
// pending retirement cycle; the established retire-before-issue model
// (rvtt-macro-tables.h derived-calendar provenance) orders a front-end
// access at that cycle after every pending event, so the launch's VD
// write is proven safe and the boundary still elides -- the equality
// direction of drain-follower-vd-write, byte-identical to the pre-fix
// compiler.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "run-boundary drain elided .drain=3 separator-credit=2" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "drain-follower-vd-write" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner drain-interrow: drain=3 rows=8" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPNOP" 21 } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);          \
      auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31, 0, 0, 0);\
      auto converted = __builtin_rvtt_sfpcast (shifted, 0);                   \
      __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0, 7);            \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void signbit_two_face_runs ()
{
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  __builtin_rvtt_ttdstface ();
  ROW ();
  ROW ();
  ROW ();
  ROW ();
}

#undef ROW
