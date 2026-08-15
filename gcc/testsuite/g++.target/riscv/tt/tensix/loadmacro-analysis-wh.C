// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-analyze-loadmacro -fdump-rtl-rvtt_loadmacro-details" }
// WH needs one explicit latency NOP between the multiply and store.
// { dg-final { scan-rtl-dump-times "SFPLOADMACRO candidate: .*words=5 loads=2 stores=1 emit=no" 1 "rvtt_loadmacro" } }
// { dg-final { scan-rtl-dump-times "reject=subunit-calendar-missing" 1 "rvtt_loadmacro" } }
// { dg-final { scan-rtl-dump-times "reject=simulator-event-model-missing" 1 "rvtt_loadmacro" } }
// { dg-final { scan-assembler-not "SFPLOADMACRO" } }

#include "loadmacro-analysis-body.h"
