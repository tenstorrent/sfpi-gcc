#include "rtl-lane-reset.h"

// Check that IMS repair leaves the compact select calendar alone and arbitration keeps the formation.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-dst-autoincr -mtt-tensix-macro-ims -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "Macro-planner schedule-candidate: cc-compact" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "ims-repair" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner ims-arbitration: formed=5343 replay-alt=6400 .centislots; run-rows=8 ii=3 row-words=8. -> form" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump "Macro-planner formed: rows=8" "rvtt_macro_planner" } }

#define SELECT_ADDR_MODE 7
#define SELECT_COND_MODE 6
#include "macro-planner-select-body.h"

__attribute__((noinline)) void select_uniform_compact ()
{
  RVTT_TEST_LANE_RESET ();
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  SELECT_ROWS_8 ();
}
