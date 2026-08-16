// No capability table is proven for QSR: scheduling refuses before any
// search with the tables' stable refusal name.
// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner-analyze -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner schedule-refusal: target-macro-encoding-unproven" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner schedule: ii=" "rvtt_macro_planner" } }

#include "loadmacro-periodic-minmax-body.h"
