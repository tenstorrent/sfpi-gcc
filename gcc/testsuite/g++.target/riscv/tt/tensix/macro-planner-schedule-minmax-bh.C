// The scheduler derives the three-slot alternating-VD calendar from the
// row dataflow and the capability tables: launch 0 carries the first load
// and hosts the launched simple event, the second load issues explicitly,
// launch 1 carries the delayed store and absorbs the typed Dst stride.
// The per-event programmed delays of this shape are architecturally
// unproven (NOTES-wp6-prep.md 9(g)), so the schedule refuses by name
// rather than guessing -- a WP7-blocking item.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner-analyze -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner schedule: ii=3 issues=3 launches=2 explicit=1 launched-events=2 vd=alternating drain=unproven" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner issue 0: launch macro=0 carries=load hosted=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner issue 1: explicit subunit=load" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner issue 2: launch macro=1 carries=store hosted=1 absorbs-stride=2" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner schedule-refusal: event-delay-unproven" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

#include "loadmacro-periodic-minmax-body.h"
