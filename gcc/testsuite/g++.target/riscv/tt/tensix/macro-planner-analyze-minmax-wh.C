// Check that analysis-only discovery finds eight isomorphic min/max rows on Wormhole without changing code.
// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-macro-planner-analyze -fdump-rtl-rvtt_macro_planner-details" }
// { dg-final { scan-rtl-dump-times "Macro-planner region: rows=8 row-len=4 runs=1 stride=2 loop=no" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-times "Macro-planner row-subunits: load,load,simple,store" 1 "rvtt_macro_planner" } }
// { dg-final { scan-rtl-dump-not "Macro-planner refusal" "rvtt_macro_planner" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }
// { dg-final { scan-assembler-not "SFPCONFIG" } }

#include "loadmacro-periodic-minmax-body.h"
