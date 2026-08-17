// WP9: the predicated-select (TTNN Where) shape in raw typed builtins:
// three loads, a predicate write, a lane-predicated merge, the in-row
// all-lanes restore, a store.  The CC-template descriptor derives and
// proves the full CC model, but this single row carries no typed
// ambient enable and a one-row region does not materialize one (the
// WP10 materialized proof keeps the WP9 multi-row scope), so formation
// refuses the missing lane proof and the bytes stay explicit.
// Default-ON promotion of -mtt-tensix-optimize-dst-ownership: the (now
// default-on) ownership fold removes this row's provable-identity Dst
// reload before the planner runs, so the CC template no longer derives
// the pinned refusal trail (pre-existing interaction, reproduced on the
// pre-promotion compiler with the explicit flag).  Pin the -mno-
// spelling: the test's subject is the WP9 single-row refusal on the
// unfolded shape.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mno-tt-tensix-optimize-dst-ownership -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-cc: sense=complement" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "formation-refusal: all-lanes-proof-missing" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-times "SFPSETCC" 1 } }
// { dg-final { scan-assembler-times "TTINCRWC" 1 } }

__attribute__((noinline)) void where_shape ()
{
  auto condition = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 2, 7);
  auto on_true = __builtin_rvtt_sfpload (nullptr, 0, 0, 32, 6, 7);
  auto on_false = __builtin_rvtt_sfpload (nullptr, 0, 0, 64, 6, 7);
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfpsetcc_v (condition, 2);
  auto result = __builtin_rvtt_sfpassign_lv (on_false, on_true);
  __builtin_rvtt_sfppopc (0);
  __builtin_rvtt_sfpstore (nullptr, result, 0, 0, 0, 6, 7);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
}
