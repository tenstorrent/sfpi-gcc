// Two eight-row runs separated only by the typed architectural face
// advance: the separator is classified by its pure Dst/RWC effect set
// (never opcode identity) and both runs share one region.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner-analyze -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner region: rows=16 row-len=4 runs=2 stride=2 loop=no" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner refusal" "rvtt_macro_planner" } }

#define MINMAX_ROW()                                                          \
  do                                                                          \
    {                                                                         \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);               \
      auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);              \
      auto pair = __builtin_rvtt_sfpswap (a, b, 1);                           \
      auto result = __builtin_rvtt_sfpselect2 (pair, 0);                      \
      __builtin_rvtt_sfpstore (nullptr, result, 128, 0, 0, 0, 7);             \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

__attribute__((noinline)) void analyze_two_runs_face ()
{
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  __builtin_rvtt_ttdstface ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
  MINMAX_ROW ();
}

#undef MINMAX_ROW
