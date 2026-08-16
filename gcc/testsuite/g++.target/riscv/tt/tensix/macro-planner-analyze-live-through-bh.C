// Near miss: the last row's stored value escapes the region (an extra
// store consumes it afterwards).  The escaping consumer is not a closed
// row, and the region's per-row closure proof names the live-through.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner-analyze -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner refusal: row-live-through" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner refusal: row-not-closed" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner region:" "rvtt_macro_planner" } }

#define ROW()                                                                 \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);               \
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);              \
      auto pair = __builtin_rvtt_sfpswap (a, b, 1);                           \
      auto result = __builtin_rvtt_sfpselect2 (pair, 0);                      \
      __builtin_rvtt_sfpstore (nullptr, result, 128, 0, 0, 0, 7);             \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0)

__attribute__((noinline)) void live_through_row ()
{
  { ROW (); }
  { ROW (); }
  { ROW (); }
  { ROW (); }
  { ROW (); }
  { ROW (); }
  { ROW (); }
  ROW ();
  __builtin_rvtt_sfpstore (nullptr, result, 300, 0, 0, 0, 7);
}

#undef ROW
