// WP9 lane-proof peel: the same eight select rows WITHOUT any typed
// ambient enable (the real LLK kernels establish the lane state through
// opaque init the typed IR cannot see).  The first row stays explicit
// in place -- its own typed all-lanes restore is the proven entry
// enable for the formed remainder -- and seven rows form.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-cc: sense=complement" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=7 runs=1 lane-proof=peeled-first-row" "rvtt_macro_planner" } }
// The peeled first row keeps its explicit predicate write, merge,
// restore, and store; the formed remainder contributes 14 launches and
// 7 explicit payload loads.
// { dg-final { scan-assembler-times "\\.ttinsn" 14 } }
// { dg-final { scan-assembler-times "SFPSETCC" 1 } }
// { dg-final { scan-assembler-times "SFPLOAD\\tL0, 32" 7 } }
// { dg-final { scan-assembler-times "SFPSTORE" 1 } }
// { dg-final { scan-assembler-times "SFPENCC" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 8 } }

#define ROW()                                                                 \
  do                                                                          \
    {                                                                         \
      auto condition = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 2, 7);       \
      auto on_true = __builtin_rvtt_sfpload (nullptr, 32, 0, 0, 6, 7);        \
      auto on_false = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 6, 7);       \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfpsetcc_v (condition, 2);                               \
      auto result = __builtin_rvtt_sfpassign_lv (on_false, on_true);          \
      __builtin_rvtt_sfppopc (0);                                             \
      __builtin_rvtt_sfpstore (nullptr, result, 0, 0, 0, 6, 7);               \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

__attribute__((noinline)) void select_rows_unproven_entry ()
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
