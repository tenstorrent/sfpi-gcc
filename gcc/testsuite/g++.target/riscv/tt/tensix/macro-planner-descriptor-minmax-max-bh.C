// Check that the max form, where the second swap result reaches the store, sets template mod 9.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner-analyze -mtt-tensix-macro-planner-verify -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=0: 0x920002c9" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner descriptor-word dest=1: 0x940000d6" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner verify: ok" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "descriptor-refusal" "rvtt_macro_planner" } }

#define RESULT_INDEX 1
#include "loadmacro-periodic-minmax-body.h"
