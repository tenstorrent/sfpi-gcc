// On QSR the select shape is admitted by discovery and refuses at
// schedule time by the capability tables' stable name -- no macro
// encoding is proven for QSR.  Every byte stays explicit.
// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump "target-macro-encoding-unproven" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner formed" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "\\.ttinsn" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }
// { dg-final { scan-assembler "SFPSETCC" } }

#define SELECT_ADDR_MODE 3
#include "macro-planner-select-body.h"

__attribute__((noinline)) void select_rows_qsr ()
{
  SELECT_ROWS_8 ();
}
