// Unary cast/round row: the one shape with fully documented per-event
// delays (Simple d0, Round d1, Store d2).  The derived schedule passes
// the subunit-occupancy, shared-port, and latency checks and computes the
// drain by the generic greatest-remaining-delay rule -- no refusal.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner-analyze -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner schedule: ii=1 issues=1 launches=1 explicit=0 launched-events=3 vd=alternating drain=2" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner issue 0: launch macro=0 carries=load\\+store hosted=3 absorbs-stride=2" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner schedule-refusal" "rvtt_macro_planner" } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                          \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);          \
      auto cast = __builtin_rvtt_sfpcast (loaded, 0);                         \
      auto rounded                                                            \
	= __builtin_rvtt_sfpstochrnd_i (nullptr, cast, 0, 0, 0, 1, 0);        \
      __builtin_rvtt_sfpstore (nullptr, rounded, 0, 0, 0, 2, 7);              \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                          \
  while (0)

__attribute__((noinline)) void cast_round_rows ()
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
