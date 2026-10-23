#include "rtl-lane-reset.h"

// Check that the select shape refuses at schedule time on QSR and stays explicit.
// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "target-macro-encoding-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not {SFPCONFIG[ \t]+([0-9]|1[0-4]),} } }
// { dg-final { scan-assembler "SFPSETCC" } }

#define SELECT_ADDR_MODE 3
#include "macro-planner-select-body.h"

__attribute__((noinline)) void select_rows_qsr ()
{
  RVTT_TEST_LANE_RESET ();
  SELECT_ROWS_8 ();
}
