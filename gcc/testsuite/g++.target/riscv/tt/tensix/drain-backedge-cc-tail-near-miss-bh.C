// Near miss (E1, refused EARLIER than the drain proof): a CC write in
// the loop tail already breaks the loop-scoped configuration-ownership
// window, so the region never forms -- the backedge elision is never
// consulted and no compensation appears.  The CC hazard class is thus
// closed at formation; the drain walk's own drain-cc-live refusal
// remains defense-in-depth for shapes formation admits.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner -mtt-tensix-macro-planner-verify -mtt-tensix-optimize-drain-schedule -fdump-rtl-rvtt_macro_planner" }
// { dg-final { scan-rtl-dump "Macro-planner formation-refusal: loop-body-not-owned" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "loop-backedge drain elided" "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "exit compensation" "rvtt_macro_planner" } }

#define SELECT_ADDR_MODE 7
#define DRAIN_LOOP_TAIL() __builtin_rvtt_sfpencc (3, 10)
#include "drain-backedge-select-loop-body.h"
