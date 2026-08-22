// Lane FL (FH-4): the run-boundary drain elision's follower proof must
// count the follower LAUNCH word's own front-end VD write.  A fixed-VD
// VALUE carrier (the lane-EV corruption class) re-targets the SAME
// register every row while the previous run's hosted events still pend
// up to drain_slots past its launch; when the inter-run separator is
// the absorbable INC class (zero slot credit), the next run's first
// launch issues INSIDE the pending horizon and its VD write races the
// in-flight consumers.  The boundary refuses BY NAME
// (drain-follower-vd-write) and every run keeps the full derived
// drain.  The alternating-VD envelope, store-only sacrificial VDs, and
// the CC-template model keep today's bytes (see
// drain-schedule-minmax-faces-bh.C, which still elides).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner drain-refusal: drain-follower-vd-write" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner drain-interrow: drain=3 rows=8" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "drain elided" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "drain-elided" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-times "SFPNOP" 24 } }

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

__attribute__((noinline)) void signbit_two_inc_runs ()
{
  ROW ();
  ROW ();
  ROW ();
  ROW ();
  __builtin_rvtt_ttincrwc (0, 4, 0, 0);
  ROW ();
  ROW ();
  ROW ();
  ROW ();
}

#undef ROW
