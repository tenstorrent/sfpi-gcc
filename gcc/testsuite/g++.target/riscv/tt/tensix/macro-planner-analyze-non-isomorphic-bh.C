// Near miss: one row loads a different address, breaking pairwise
// isomorphism to the first row; the discovery names the refusal and the
// remaining consecutive rows still form their own smaller regions.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner-analyze -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner refusal: row-not-isomorphic" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner region: rows=4 row-len=4 runs=1 stride=2 loop=no" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner region: rows=3 row-len=4 runs=1 stride=2 loop=no" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner region: rows=8" "rvtt_macro_planner" } }

#define ROW(A1)                                                               \
  do                                                                          \
    {                                                                         \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfppopc (0);                                             \
      auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);               \
      auto b = __builtin_rvtt_sfpload (nullptr, A1, 0, 0, 0, 7);              \
      auto pair = __builtin_rvtt_sfpswap (a, b, 1);                           \
      auto result = __builtin_rvtt_sfpselect2 (pair, 0);                      \
      __builtin_rvtt_sfpstore (nullptr, result, 128, 0, 0, 0, 7);             \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

__attribute__((noinline)) void non_isomorphic_row ()
{
  ROW (64);
  ROW (64);
  ROW (64);
  ROW (64);
  ROW (96);
  ROW (64);
  ROW (64);
  ROW (64);
}

#undef ROW
