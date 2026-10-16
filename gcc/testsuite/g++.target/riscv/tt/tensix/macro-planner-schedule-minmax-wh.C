// Check that the Wormhole minmax row derives the three-slot calendar and refuses its unproven event delays by name.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner-analyze -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner schedule: ii=3 issues=3 launches=2 explicit=1 launched-events=2 vd=alternating drain=unproven" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner issue 0: launch macro=0 carries=load hosted=1" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner issue 1: explicit subunit=load" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner issue 2: launch macro=1 carries=store hosted=1 absorbs-stride=2" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner schedule-refusal: event-delay-unproven" 1 "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

#include "loadmacro-periodic-minmax-body.h"
