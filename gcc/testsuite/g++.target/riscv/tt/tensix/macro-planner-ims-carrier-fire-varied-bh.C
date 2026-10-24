#include "rtl-lane-reset.h"

// Check that the upward-carrier formation fires with a different shift immediate and operand address.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-macro-ims -mtt-tensix-macro-ims-carrier -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump-times "Macro-planner upward-carrier: seed=2 chain=.3,4. reload-vd=3 prefix-clones=0 ii=13->12" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner upward-carrier: formed .ii=12, was 13." 1 "rvtt_macro_planner" } }
// The varied -17 immediate lands in the shared SHFT2 word:
// { dg-final { scan-rtl-dump "Macro-planner descriptor-word dest=1: 0x94fef0d6" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner descriptor-word dest=1: 0x94fe90d6" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner formed: rows=8 runs=1" 1 "rvtt_macro_planner" } }

#include "macro-planner-ims-carrier-row.h"

__attribute__((noinline)) void paired_shift_product_rows_varied ()
{
  RVTT_TEST_LANE_RESET ();
  ROW (-17, 128); ROW (-17, 128); ROW (-17, 128); ROW (-17, 128);
  ROW (-17, 128); ROW (-17, 128); ROW (-17, 128); ROW (-17, 128);
}
