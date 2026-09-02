// IMS arbitration on a predicate-definition row (the compact select / Where class):
// the repair driver leaves the established CC candidate space untouched
// (the proven select programs own those calendars -- no ims-repair
// variants), and the arbitration prices the formed compact calendar
// BELOW the replay-delivered alternative (the measured Where win's
// delivery economics: 123*29 config + 8*3*100 + 3*100 drain vs
// 8 rows * 8 words * 100), so the formation stands byte-identically.
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
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  SELECT_ROWS_8 ();
}
