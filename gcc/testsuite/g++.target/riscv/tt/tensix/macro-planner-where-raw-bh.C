// The predicated-select (TTNN Where) shape in raw typed builtins:
// three loads, a predicate write, a lane-predicated merge, the in-row
// all-lanes restore, a store.  Before the Where hardware
// adjudication this refused the missing lane proof AFTER the 4-slot
// descriptor proved; the descriptor CC model now refuses the calendar
// itself (root cause the reference simulator -- the store's lane mask is
// live at execution and this calendar retires its all-lanes restore
// in the store's own cycle): the mixed-mode compact candidate refuses
// its descriptor by name and the established calendar's descriptor
// refuses cc-restore-store-race, so no descriptor is synthesized and
// the bytes stay explicit.
// Default-ON promotion of -mtt-tensix-optimize-dst-ownership: the (now
// default-on) ownership fold removes this raw body's provable-identity
// Dst reload before the planner runs, and the folded shape refuses
// earlier (cc-template-unproved) without ever reaching the pinned
// race refusal.  Pin the -mno- spelling: the test's subject
// is the adjudicated refusal on the unfolded shape.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mno-tt-tensix-optimize-dst-ownership -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-refusal: cc-template-unproved" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner descriptor-refusal: cc-restore-store-race" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner schedule-refusal: cc-separator-kept-silicon-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner descriptor-cc:" "rvtt_macro_planner" } }
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
