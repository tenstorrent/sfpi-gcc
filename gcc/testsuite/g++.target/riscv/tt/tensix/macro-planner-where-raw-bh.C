// WP8: the predicated-select (TTNN Where) shape in raw typed builtins:
// three loads, a predicate write, a lane-predicated move, a store.  The
// predicate write inside the slice would need a CC-manipulating
// instruction template; no proven CC-template program exists, so the
// region refuses cc-template-unsupported and the bytes stay explicit.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "cc-template-unsupported" "rvtt_macro_planner" } }
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
