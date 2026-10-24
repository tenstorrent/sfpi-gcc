// A public no-anchor entry cannot lose its configuration prefix merely
// because the TU also contains a fully reset direct caller.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-init-hoist -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "init-hoist: closure .callee-entry-unproved." "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "init contract hoisted to caller" "rvtt_macro_planner" } }
// { dg-final { scan-assembler "SFPCONFIG" } }

#include "rtl-lane-reset.h"
#define MINMAX_FOUR_FACE_RUNS 1
#define MINMAX_VISIBILITY
#include "loadmacro-periodic-minmax-inplace-body.h"

void public_entry_caller (unsigned tiles)
{
  RVTT_TEST_LANE_RESET ();
  do
    {
      periodic_minmax_inplace ();
      __builtin_rvtt_ttdstface ();
    }
  while (--tiles);
}
